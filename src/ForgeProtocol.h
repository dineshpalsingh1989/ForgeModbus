#pragma once
#include <stddef.h>
#include <stdint.h>

// Portable Modbus TCP protocol core. No Arduino, socket or RTOS dependencies.
namespace forge {

enum class Area : uint8_t { Coils, DiscreteInputs, InputRegisters, HoldingRegisters };

// Modbus exception codes a hook may return. Zero means "no exception".
enum Exception : uint8_t {
  Ok = 0,
  IllegalFunction = 1,
  IllegalDataAddress = 2,
  IllegalDataValue = 3,
  DeviceFailure = 4,
  DeviceBusy = 6,
};

// One request touching one table. Passed to hooks; valid only during the call.
struct Access {
  Area area;
  uint8_t unit;
  uint16_t address;  // protocol address of the first item
  uint16_t count;    // number of coils/registers
  // Requested values of a write. Not meaningful for reads.
  uint16_t value(uint16_t i) const { return uint16_t(data[2 * i]) << 8 | data[2 * i + 1]; }
  bool bit(uint16_t i) const { return (data[i >> 3] >> (i & 7)) & 1; }
  const uint8_t* data;
};

struct Hooks {
  // Before a read: refresh table values, or return an exception code.
  uint8_t (*beforeRead)(const Access&, void* context) = nullptr;
  // Before a write: validate the requested values, or return an exception code.
  uint8_t (*beforeWrite)(const Access&, void* context) = nullptr;
  // After a write has been stored in the table.
  void (*afterWrite)(const Access&, void* context) = nullptr;
  void* context = nullptr;
};

// Application-owned storage for one table. Bits are packed: item k is
// bit (k % 8) of data[k / 8]. Address `start` maps to element 0.
struct RegisterTable { uint16_t* data = nullptr; uint16_t start = 0; uint16_t count = 0; };
struct BitTable { uint8_t* data = nullptr; uint16_t start = 0; uint16_t count = 0; };

class Protocol {
 public:
  static constexpr size_t MaxAdu = 260;
  static constexpr size_t MaxReadOnly = 4;
  static constexpr int Close = -1;   // invalid framing: drop the connection
  static constexpr int Ignore = 0;   // valid frame that gets no reply

  Protocol() = default;
  Protocol(uint16_t* holding, uint16_t count) { holding_.data = holding; holding_.count = count; }

  void holdingRegisters(uint16_t* data, uint16_t count, uint16_t start = 0) { holding_ = {data, start, count}; }
  void inputRegisters(uint16_t* data, uint16_t count, uint16_t start = 0) { input_ = {data, start, count}; }
  void coils(uint8_t* data, uint16_t count, uint16_t start = 0) { coils_ = {data, start, count}; }
  void discreteInputs(uint8_t* data, uint16_t count, uint16_t start = 0) { discrete_ = {data, start, count}; }
  // Writes overlapping a read-only range of coils or holding registers fail
  // with IllegalDataAddress. Returns false when all slots are used.
  bool readOnly(Area area, uint16_t start, uint16_t count);
  // Zero answers every unit id. Otherwise only this id and 255 are answered;
  // requests for other ids are ignored without closing the connection.
  void unitId(uint8_t id) { unit_ = id; }
  void hooks(const Hooks& hooks) { hooks_ = hooks; }

  // Complete TCP ADU in, complete TCP ADU out. Returns the response size,
  // Ignore, or Close. Input and output must not overlap; output capacity
  // must be at least MaxAdu bytes.
  int respond(const uint8_t* request, size_t size, uint8_t* response);

 private:
  struct Range { Area area; uint16_t start, count; };
  RegisterTable holding_, input_;
  BitTable coils_, discrete_;
  Range readOnly_[MaxReadOnly] = {};
  size_t readOnlyCount_ = 0;
  uint8_t unit_ = 0;
  Hooks hooks_;
  uint8_t execute(const uint8_t* q, size_t n, uint8_t* r, size_t& size);
  uint8_t read(Area area, uint8_t unit, uint16_t address, uint16_t count, uint8_t* out);
  uint8_t write(Area area, uint8_t unit, uint16_t address, uint16_t count, const uint8_t* data);
  bool mapped(Area area, uint16_t address, uint16_t count, size_t& offset) const;
  bool protectedRange(Area area, uint16_t address, uint16_t count) const;
};

// Returns total ADU size, or zero for an invalid MBAP header. Needs 6 bytes.
size_t frameSize(const uint8_t* header);

}  // namespace forge
