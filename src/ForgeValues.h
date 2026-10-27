#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// Helpers for values that span two registers, and for packed bit tables.
namespace forge {

// How a 32-bit value is laid out in two registers. A,B,C,D are the value's
// bytes from most to least significant. ABCD is the Modbus "big-endian" order;
// CDAB ("word swapped") is the other common choice among masters.
enum class WordOrder : uint8_t { ABCD, CDAB, BADC, DCBA };

inline void putU32(uint16_t* registers, uint32_t value, WordOrder order = WordOrder::ABCD) {
  uint16_t high = uint16_t(value >> 16), low = uint16_t(value);
  if (order == WordOrder::BADC || order == WordOrder::DCBA) {
    high = uint16_t(high << 8 | high >> 8);
    low = uint16_t(low << 8 | low >> 8);
  }
  const bool swapped = order == WordOrder::CDAB || order == WordOrder::DCBA;
  registers[0] = swapped ? low : high;
  registers[1] = swapped ? high : low;
}

inline uint32_t getU32(const uint16_t* registers, WordOrder order = WordOrder::ABCD) {
  const bool swapped = order == WordOrder::CDAB || order == WordOrder::DCBA;
  uint16_t high = swapped ? registers[1] : registers[0];
  uint16_t low = swapped ? registers[0] : registers[1];
  if (order == WordOrder::BADC || order == WordOrder::DCBA) {
    high = uint16_t(high << 8 | high >> 8);
    low = uint16_t(low << 8 | low >> 8);
  }
  return uint32_t(high) << 16 | low;
}

inline void putI32(uint16_t* r, int32_t v, WordOrder o = WordOrder::ABCD) { putU32(r, uint32_t(v), o); }
inline int32_t getI32(const uint16_t* r, WordOrder o = WordOrder::ABCD) { return int32_t(getU32(r, o)); }

inline void putFloat(uint16_t* registers, float value, WordOrder order = WordOrder::ABCD) {
  static_assert(sizeof(float) == 4, "IEEE 754 single precision expected");
  uint32_t bits;
  memcpy(&bits, &value, 4);
  putU32(registers, bits, order);
}

inline float getFloat(const uint16_t* registers, WordOrder order = WordOrder::ABCD) {
  const uint32_t bits = getU32(registers, order);
  float value;
  memcpy(&value, &bits, 4);
  return value;
}

// Packed bit tables, as used for coils and discrete inputs.
constexpr size_t bitBytes(size_t bits) { return (bits + 7) / 8; }
inline bool getBit(const uint8_t* bits, size_t index) { return (bits[index >> 3] >> (index & 7)) & 1; }
inline void setBit(uint8_t* bits, size_t index, bool value) {
  const uint8_t mask = uint8_t(1u << (index & 7));
  if (value) bits[index >> 3] |= mask;
  else bits[index >> 3] &= uint8_t(~mask);
}

}  // namespace forge
