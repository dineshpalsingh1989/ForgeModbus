#include "ForgeModbus.h"
#include <lwip/sockets.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

namespace forge {
namespace {
bool nonblocking(int fd) {
  const int flags = fcntl(fd, F_GETFL, 0);
  return flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}
bool retryLater() { return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR; }
// Address bytes in memory order, matching sockaddr_in::sin_addr on any endianness.
uint32_t pack(const IPAddress& ip) {
  const uint8_t bytes[4] = {ip[0], ip[1], ip[2], ip[3]};
  uint32_t value;
  memcpy(&value, bytes, 4);
  return value;
}
bool expired(uint32_t now, uint32_t since, uint32_t limit) { return limit && uint32_t(now - since) >= limit; }
}  // namespace

Server::Server() : lock_(xSemaphoreCreateRecursiveMutexStatic(&lockBuffer_)) {}
Server::Server(uint16_t* holding, uint16_t count)
    : protocol_(holding, count), lock_(xSemaphoreCreateRecursiveMutexStatic(&lockBuffer_)) {}
Server::~Server() { end(); }

bool Server::allow(const IPAddress& address, const IPAddress& mask) {
  if (allowedCount_ == MaxAllowed) return false;
  const uint32_t m = pack(mask);
  allowed_[allowedCount_++] = {pack(address) & m, m};
  return true;
}

bool Server::permitted(uint32_t address) const {
  if (!allowedCount_) return true;
  for (size_t i = 0; i < allowedCount_; ++i)
    if ((address & allowed_[i].mask) == allowed_[i].address) return true;
  return false;
}

Server::Stats Server::stats() const {
  Lock guard(*this);
  return stats_;
}

void Server::closeClient(Client& c) {
  if (c.fd >= 0) {
    close(c.fd);
    if (stats_.activeClients) --stats_.activeClients;
  }
  c.fd = -1;
  c.received = c.pending = c.sent = 0;
}

void Server::closeListener() {
  if (listener_ >= 0) close(listener_);
  listener_ = -1;
}

bool Server::openListener() {
  lastAttempt_ = millis();
  const int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (fd < 0) return false;
  int one = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
  sockaddr_in address = {};
  address.sin_family = AF_INET;
  address.sin_port = htons(config_.port);
  address.sin_addr.s_addr = htonl(INADDR_ANY);
  if (!nonblocking(fd) || bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0 ||
      listen(fd, int(MaxClients)) < 0) {
    close(fd);
    return false;
  }
  listener_ = fd;
  if (hadListener_) ++stats_.listenerRestarts;
  hadListener_ = true;
  return true;
}

bool Server::begin() {
  end();
  Lock guard(*this);
  wanted_ = true;
  hadListener_ = false;
  return openListener();
}

void Server::end() {
  stopTask();
  Lock guard(*this);
  wanted_ = false;
  for (auto& c : clients_) closeClient(c);
  closeListener();
}

void Server::acceptClient() {
  sockaddr_in peer = {};
  socklen_t length = sizeof(peer);
  const int fd = accept(listener_, reinterpret_cast<sockaddr*>(&peer), &length);
  if (fd < 0) {
    if (retryLater()) return;
    ++stats_.socketErrors;
    // Per-connection and resource errors leave the listener usable.
    if (errno != ECONNABORTED && errno != EMFILE && errno != ENFILE && errno != ENOBUFS && errno != ENOMEM)
      closeListener();  // reopened by poll() after config_.retryMs
    return;
  }
  if (!permitted(peer.sin_addr.s_addr)) { close(fd); ++stats_.denied; return; }
  Client* slot = nullptr;
  for (auto& c : clients_) if (c.fd < 0) { slot = &c; break; }
  if (!slot || !nonblocking(fd)) { close(fd); ++stats_.rejected; return; }
  int one = 1;
  if (config_.noDelay) setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
  if (config_.keepAliveSec) {
    setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &one, sizeof(one));
#if defined(TCP_KEEPIDLE) && defined(TCP_KEEPINTVL) && defined(TCP_KEEPCNT)
    int idle = config_.keepAliveSec, interval = 5, count = 3;
    setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof(idle));
    setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &interval, sizeof(interval));
    setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &count, sizeof(count));
#endif
  }
  slot->fd = fd;
  slot->received = slot->pending = slot->sent = 0;
  slot->lastActivity = millis();
  ++stats_.accepted;
  if (++stats_.activeClients > stats_.peakClients) stats_.peakClients = stats_.activeClients;
}

// Sends what the socket accepts. True when nothing remains pending.
bool Server::flush(Client& c, uint32_t now) {
  while (c.pending) {
    const int written = send(c.fd, c.tx + c.sent, c.pending - c.sent, 0);
    if (written > 0) {
      c.sent += size_t(written);
      if (c.sent == c.pending) { c.sent = c.pending = 0; c.lastActivity = now; }
    } else if (written < 0 && retryLater()) {
      return false;
    } else {
      ++stats_.socketErrors;
      closeClient(c);
      return false;
    }
  }
  return true;
}

void Server::service(Client& c, uint32_t now) {
  if (c.pending) {
    if (expired(now, c.sendStart, config_.sendTimeoutMs)) { ++stats_.sendTimeouts; closeClient(c); return; }
  } else if (c.received) {
    if (expired(now, c.requestStart, config_.requestTimeoutMs)) { ++stats_.requestTimeouts; closeClient(c); return; }
  } else if (expired(now, c.lastActivity, config_.idleTimeoutMs)) {
    ++stats_.idleTimeouts; closeClient(c); return;
  }
  // One response in flight per client: pipelined requests wait in the socket.
  if (c.pending && !flush(c, now)) return;

  // Read exactly one header, then exactly one frame.
  size_t target = c.received < 6 ? 6 : frameSize(c.rx);
  const int got = recv(c.fd, c.rx + c.received, target - c.received, 0);
  if (got == 0) { closeClient(c); return; }
  if (got < 0) {
    if (!retryLater()) { ++stats_.socketErrors; closeClient(c); }
    return;
  }
  if (c.received == 0) c.requestStart = now;
  c.received += size_t(got);
  if (c.received < 6) return;
  target = frameSize(c.rx);
  if (!target) { ++stats_.invalidFrames; closeClient(c); return; }
  if (c.received < target) return;

  ++stats_.requests;
  const int result = protocol_.respond(c.rx, c.received, c.tx);
  c.received = 0;
  if (result == Protocol::Close) { ++stats_.invalidFrames; closeClient(c); return; }
  if (result == Protocol::Ignore) { ++stats_.ignored; c.lastActivity = now; return; }
  if (c.tx[7] & 0x80) ++stats_.exceptions;
  c.pending = size_t(result);
  c.sent = 0;
  c.sendStart = now;
  flush(c, now);
}

void Server::wait(uint32_t waitMs) {
  fd_set readable, writable;
  FD_ZERO(&readable);
  FD_ZERO(&writable);
  int highest = -1;
  {
    Lock guard(*this);
    if (listener_ >= 0) { FD_SET(listener_, &readable); highest = listener_; }
    for (auto& c : clients_) {
      if (c.fd < 0) continue;
      FD_SET(c.fd, c.pending ? &writable : &readable);
      if (c.fd > highest) highest = c.fd;
    }
  }
  if (highest < 0) { delay(waitMs); return; }
  timeval timeout = {};
  timeout.tv_sec = long(waitMs / 1000);
  timeout.tv_usec = long(waitMs % 1000) * 1000;
  select(highest + 1, &readable, &writable, nullptr, &timeout);  // poll() handles every outcome
}

void Server::poll(uint32_t waitMs) {
  if (waitMs) wait(waitMs);
  Lock guard(*this);
  if (!wanted_) return;
  if (listener_ < 0) {
    if (uint32_t(millis() - lastAttempt_) >= config_.retryMs) openListener();
    if (listener_ < 0) return;
  }
  acceptClient();  // bounded work: at most one new connection per poll
  const uint32_t now = millis();
  for (auto& c : clients_) if (c.fd >= 0) service(c, now);
}

bool Server::startTask(uint32_t stackBytes, UBaseType_t priority, BaseType_t core) {
  if (task_) return false;
  taskStop_ = false;
  TaskHandle_t handle = nullptr;
  // ESP32 FreeRTOS takes the stack depth in bytes.
  if (xTaskCreatePinnedToCore(taskMain, "forge-modbus", stackBytes, this, priority, &handle, core) != pdPASS)
    return false;
  task_ = handle;
  return true;
}

void Server::stopTask() {
  TaskHandle_t handle = task_;
  if (!handle) return;
  taskStop_ = true;
  if (handle == xTaskGetCurrentTaskHandle()) return;  // the task exits after this poll
  while (task_) delay(5);
}

void Server::taskMain(void* argument) {
  Server& server = *static_cast<Server*>(argument);
  while (!server.task_) delay(1);  // startTask() publishes the handle after creation
  while (!server.taskStop_) server.poll(50);
  server.task_ = nullptr;
  vTaskDelete(nullptr);
}

}  // namespace forge
