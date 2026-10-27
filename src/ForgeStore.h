#pragma once
#include <Preferences.h>
#include <atomic>
#include "ForgeModbus.h"

namespace forge {

// Keeps selected tables in flash (NVS) so values survive a reboot.
// Call changed() from an afterWrite hook and loop() regularly. Saves are
// delayed by delayMs so a burst of writes costs one flash write per table.
class Store {
 public:
  static constexpr size_t MaxEntries = 8;
  uint32_t delayMs = 5000;

  // With a server, saves hold its lock so the tables are copied consistently.
  explicit Store(Server* server = nullptr) : server_(server) {}
  ~Store() { end(); }
  Store(const Store&) = delete;
  Store& operator=(const Store&) = delete;

  // Namespace and keys are limited to 15 characters by NVS.
  bool begin(const char* name = "forge");
  void end();
  bool add(const char* key, uint16_t* registers, uint16_t count);
  bool addBits(const char* key, uint8_t* bits, uint16_t bitCount);
  // Restores every saved table whose stored size matches. Returns false if any is missing.
  bool load();
  bool save();
  void changed();
  void loop();

 private:
  struct Entry { const char* key; void* data; size_t bytes; };
  Server* server_;
  Preferences preferences_;
  Entry entries_[MaxEntries] = {};
  size_t count_ = 0;
  bool open_ = false;
  std::atomic<uint32_t> dirtySince_{0};  // 0 = nothing to save
};

}  // namespace forge
