#include "ForgeStore.h"

namespace forge {

bool Store::begin(const char* name) {
  end();
  open_ = preferences_.begin(name, false);
  return open_;
}

void Store::end() {
  if (!open_) return;
  if (dirtySince_) save();
  preferences_.end();
  open_ = false;
}

bool Store::add(const char* key, uint16_t* registers, uint16_t count) {
  if (count_ == MaxEntries || !registers || !count) return false;
  entries_[count_++] = {key, registers, size_t(count) * 2};
  return true;
}

bool Store::addBits(const char* key, uint8_t* bits, uint16_t bitCount) {
  if (count_ == MaxEntries || !bits || !bitCount) return false;
  entries_[count_++] = {key, bits, bitBytes(bitCount)};
  return true;
}

bool Store::load() {
  if (!open_) return false;
  bool all = true;
  if (server_) server_->lock();
  for (size_t i = 0; i < count_; ++i) {
    const Entry& e = entries_[i];
    if (preferences_.getBytesLength(e.key) == e.bytes) preferences_.getBytes(e.key, e.data, e.bytes);
    else all = false;
  }
  if (server_) server_->unlock();
  return all;
}

bool Store::save() {
  if (!open_) return false;
  dirtySince_ = 0;  // writes arriving during the save mark the store dirty again
  bool ok = true;
  if (server_) server_->lock();
  for (size_t i = 0; i < count_; ++i) {
    const Entry& e = entries_[i];
    if (preferences_.putBytes(e.key, e.data, e.bytes) != e.bytes) ok = false;
  }
  if (server_) server_->unlock();
  return ok;
}

void Store::changed() {
  uint32_t expected = 0;
  dirtySince_.compare_exchange_strong(expected, millis() | 1u);
}

void Store::loop() {
  const uint32_t since = dirtySince_;
  if (since && uint32_t(millis() - since) >= delayMs) save();
}

}  // namespace forge
