#pragma once
#include <Arduino.h>
#include <IPAddress.h>
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include "ForgeProtocol.h"
#include "ForgeValues.h"

#ifndef FORGE_MAX_CLIENTS
#define FORGE_MAX_CLIENTS 4
#endif

namespace forge {

// Modbus TCP server for ESP32 (Wi-Fi or Ethernet), using lwIP sockets.
//
// Threading: poll() runs the protocol and hooks with the server lock held.
// Code in another task that touches the tables must hold Server::Lock.
class Server {
 public:
  static constexpr size_t MaxClients = FORGE_MAX_CLIENTS;
  static constexpr size_t MaxAllowed = 4;

  struct Config {
    uint16_t port = 502;
    uint32_t idleTimeoutMs = 0;        // close clients idle this long; 0 keeps them
    uint32_t requestTimeoutMs = 5000;  // a started request must arrive completely in time
    uint32_t sendTimeoutMs = 5000;     // a response must be fully sent in time
    uint16_t keepAliveSec = 30;        // TCP keepalive idle time; 0 disables it
    bool noDelay = true;               // TCP_NODELAY: send each reply immediately
    uint32_t retryMs = 1000;           // wait between attempts to reopen the listener
  };

  struct Stats {
    uint32_t accepted = 0, rejected = 0, denied = 0;
    uint32_t requests = 0, exceptions = 0, ignored = 0, invalidFrames = 0;
    uint32_t idleTimeouts = 0, requestTimeouts = 0, sendTimeouts = 0;
    uint32_t socketErrors = 0, listenerRestarts = 0;
    uint8_t activeClients = 0, peakClients = 0;
  };

  Server();
  Server(uint16_t* holding, uint16_t count);
  ~Server();
  Server(const Server&) = delete;
  Server& operator=(const Server&) = delete;

  // Table setup. Call before begin(), or while holding Server::Lock.
  // Coils and discrete inputs are packed bits; see forge::bitBytes().
  void holdingRegisters(uint16_t* data, uint16_t count, uint16_t start = 0) { protocol_.holdingRegisters(data, count, start); }
  void inputRegisters(uint16_t* data, uint16_t count, uint16_t start = 0) { protocol_.inputRegisters(data, count, start); }
  void coils(uint8_t* data, uint16_t count, uint16_t start = 0) { protocol_.coils(data, count, start); }
  void discreteInputs(uint8_t* data, uint16_t count, uint16_t start = 0) { protocol_.discreteInputs(data, count, start); }
  bool readOnly(Area area, uint16_t start, uint16_t count) { return protocol_.readOnly(area, start, count); }
  void unitId(uint8_t id) { protocol_.unitId(id); }
  void hooks(const Hooks& hooks) { protocol_.hooks(hooks); }

  // Settings take effect at the next begin().
  Config& config() { return config_; }
  // With any entries, only matching client addresses may connect.
  bool allow(const IPAddress& address, const IPAddress& mask = IPAddress(255, 255, 255, 255));
  void clearAllowed() { allowedCount_ = 0; }

  // Opens the listener. If that fails (for example, no network yet), poll()
  // keeps retrying every config().retryMs until end().
  bool begin();
  bool begin(uint16_t port) { config_.port = port; return begin(); }
  // Services the server once. waitMs > 0 sleeps until network activity or
  // the time elapses, so a dedicated loop does not spin.
  void poll(uint32_t waitMs = 0);
  void end();
  bool listening() const { return listener_ >= 0; }
  Stats stats() const;

  // Runs poll() in its own FreeRTOS task. Do not also call poll() yourself.
  bool startTask(uint32_t stackBytes = 4096, UBaseType_t priority = 1, BaseType_t core = tskNO_AFFINITY);
  void stopTask();

  // Recursive lock shared with poll(). Prefer the RAII Lock below.
  void lock() const { xSemaphoreTakeRecursive(lock_, portMAX_DELAY); }
  void unlock() const { xSemaphoreGiveRecursive(lock_); }
  class Lock {
   public:
    explicit Lock(const Server& server) : server_(server) { server_.lock(); }
    ~Lock() { server_.unlock(); }
    Lock(const Lock&) = delete;
    Lock& operator=(const Lock&) = delete;
   private:
    const Server& server_;
  };

 private:
  struct Client {
    int fd = -1;
    uint8_t rx[Protocol::MaxAdu] = {}, tx[Protocol::MaxAdu] = {};
    size_t received = 0, pending = 0, sent = 0;
    uint32_t lastActivity = 0, requestStart = 0, sendStart = 0;
  };
  struct Allowed { uint32_t address, mask; };

  Protocol protocol_;
  Config config_;
  Stats stats_;
  Client clients_[MaxClients];
  Allowed allowed_[MaxAllowed] = {};
  size_t allowedCount_ = 0;
  int listener_ = -1;
  bool wanted_ = false;
  uint32_t lastAttempt_ = 0;
  bool hadListener_ = false;
  StaticSemaphore_t lockBuffer_;
  SemaphoreHandle_t lock_;
  std::atomic<TaskHandle_t> task_{nullptr};
  std::atomic<bool> taskStop_{false};

  bool openListener();
  void closeListener();
  void acceptClient();
  void service(Client& client, uint32_t now);
  bool flush(Client& client, uint32_t now);
  void closeClient(Client& client);
  bool permitted(uint32_t address) const;
  void wait(uint32_t waitMs);
  static void taskMain(void* server);
};

}  // namespace forge
