#include "ForgeProtocol.h"
#include <string.h>

namespace forge {
namespace {
uint16_t word(const uint8_t* p) { return uint16_t(uint16_t(p[0]) << 8 | p[1]); }
void put(uint8_t* p, uint16_t value) { p[0] = uint8_t(value >> 8); p[1] = uint8_t(value & 255); }
bool isRegisters(Area area) { return area == Area::InputRegisters || area == Area::HoldingRegisters; }
// Modbus quantity limits (Modbus Application Protocol v1.1b3, section 6).
constexpr uint16_t MaxReadBits = 2000, MaxWriteBits = 1968;
constexpr uint16_t MaxReadRegisters = 125, MaxWriteRegisters = 123;
}  // namespace

size_t frameSize(const uint8_t* h) {
  const uint16_t length = word(h + 4);
  return word(h + 2) == 0 && length >= 2 && length <= 254 ? 6 + length : 0;
}

bool Protocol::readOnly(Area area, uint16_t start, uint16_t count) {
  if (count == 0 || readOnlyCount_ == MaxReadOnly) return false;
  readOnly_[readOnlyCount_++] = {area, start, count};
  return true;
}

bool Protocol::mapped(Area area, uint16_t address, uint16_t count, size_t& offset) const {
  uint16_t start, size;
  bool present;
  switch (area) {
    case Area::Coils: start = coils_.start; size = coils_.count; present = coils_.data; break;
    case Area::DiscreteInputs: start = discrete_.start; size = discrete_.count; present = discrete_.data; break;
    case Area::InputRegisters: start = input_.start; size = input_.count; present = input_.data; break;
    default: start = holding_.start; size = holding_.count; present = holding_.data; break;
  }
  if (!present || address < start) return false;
  offset = size_t(address - start);
  return offset < size && count <= size - offset;
}

bool Protocol::protectedRange(Area area, uint16_t address, uint16_t count) const {
  for (size_t i = 0; i < readOnlyCount_; ++i) {
    const Range& p = readOnly_[i];
    if (p.area == area && uint32_t(address) < uint32_t(p.start) + p.count &&
        uint32_t(p.start) < uint32_t(address) + count) return true;
  }
  return false;
}

uint8_t Protocol::read(Area area, uint8_t unit, uint16_t address, uint16_t count, uint8_t* out) {
  size_t offset;
  if (!mapped(area, address, count, offset)) return IllegalDataAddress;
  if (hooks_.beforeRead) {
    const Access access = {area, unit, address, count, nullptr};
    if (const uint8_t error = hooks_.beforeRead(access, hooks_.context)) return error;
  }
  if (isRegisters(area)) {
    const uint16_t* table = area == Area::HoldingRegisters ? holding_.data : input_.data;
    for (size_t i = 0; i < count; ++i) put(out + 2 * i, table[offset + i]);
  } else {
    const uint8_t* table = area == Area::Coils ? coils_.data : discrete_.data;
    memset(out, 0, (count + 7u) / 8u);
    for (size_t i = 0; i < count; ++i) {
      const size_t k = offset + i;
      if ((table[k >> 3] >> (k & 7)) & 1) out[i >> 3] |= uint8_t(1u << (i & 7));
    }
  }
  return Ok;
}

uint8_t Protocol::write(Area area, uint8_t unit, uint16_t address, uint16_t count, const uint8_t* data) {
  size_t offset;
  if (!mapped(area, address, count, offset) || protectedRange(area, address, count)) return IllegalDataAddress;
  const Access access = {area, unit, address, count, data};
  if (hooks_.beforeWrite) {
    if (const uint8_t error = hooks_.beforeWrite(access, hooks_.context)) return error;
  }
  if (area == Area::HoldingRegisters) {
    for (uint16_t i = 0; i < count; ++i) holding_.data[offset + i] = access.value(i);
  } else {
    for (uint16_t i = 0; i < count; ++i) {
      const size_t k = offset + i;
      const uint8_t mask = uint8_t(1u << (k & 7));
      if (access.bit(i)) coils_.data[k >> 3] |= mask;
      else coils_.data[k >> 3] &= uint8_t(~mask);
    }
  }
  if (hooks_.afterWrite) hooks_.afterWrite(access, hooks_.context);
  return Ok;
}

// Validation order follows the specification: function, then quantity and
// length (IllegalDataValue), then address, then execution.
uint8_t Protocol::execute(const uint8_t* q, size_t n, uint8_t* r, size_t& size) {
  const uint8_t function = q[7], unit = q[6];
  const size_t pdu = n - 7;  // function code and data
  switch (function) {
    case 1:
    case 2: {
      if (pdu != 5) return IllegalDataValue;
      const uint16_t address = word(q + 8), count = word(q + 10);
      if (count == 0 || count > MaxReadBits) return IllegalDataValue;
      const uint8_t bytes = uint8_t((count + 7) / 8);
      if (const uint8_t e = read(function == 1 ? Area::Coils : Area::DiscreteInputs, unit, address, count, r + 9)) return e;
      r[8] = bytes;
      size = 9 + bytes;
      return Ok;
    }
    case 3:
    case 4: {
      if (pdu != 5) return IllegalDataValue;
      const uint16_t address = word(q + 8), count = word(q + 10);
      if (count == 0 || count > MaxReadRegisters) return IllegalDataValue;
      const Area area = function == 3 ? Area::HoldingRegisters : Area::InputRegisters;
      if (const uint8_t e = read(area, unit, address, count, r + 9)) return e;
      r[8] = uint8_t(count * 2);
      size = 9 + 2 * size_t(count);
      return Ok;
    }
    case 5: {
      if (pdu != 5) return IllegalDataValue;
      const uint16_t value = word(q + 10);
      if (value != 0xFF00 && value != 0) return IllegalDataValue;
      const uint8_t bit = value ? 1 : 0;
      if (const uint8_t e = write(Area::Coils, unit, word(q + 8), 1, &bit)) return e;
      break;
    }
    case 6:
      if (pdu != 5) return IllegalDataValue;
      if (const uint8_t e = write(Area::HoldingRegisters, unit, word(q + 8), 1, q + 10)) return e;
      break;
    case 15:
    case 16: {
      if (pdu < 6) return IllegalDataValue;
      const uint16_t address = word(q + 8), count = word(q + 10);
      const size_t bytes = q[12];
      const bool bits = function == 15;
      const uint16_t limit = bits ? MaxWriteBits : MaxWriteRegisters;
      const size_t expected = bits ? (count + 7u) / 8u : 2u * count;
      if (count == 0 || count > limit || bytes != expected || pdu != 6 + bytes) return IllegalDataValue;
      if (const uint8_t e = write(bits ? Area::Coils : Area::HoldingRegisters, unit, address, count, q + 13)) return e;
      break;
    }
    default:
      return IllegalFunction;
  }
  memcpy(r + 8, q + 8, 4);  // writes echo address and value/quantity
  size = 12;
  return Ok;
}

int Protocol::respond(const uint8_t* q, size_t n, uint8_t* r) {
  if (n < 8 || frameSize(q) != n) return Close;
  const uint8_t unit = q[6], function = q[7];
  if (function & 0x80) return Ignore;  // an exception code is never a valid request
  if (unit_ && unit != unit_ && unit != 255) return Ignore;
  memcpy(r, q, 8);  // transaction, protocol, unit and function are preserved
  size_t size = 0;
  if (const uint8_t error = execute(q, n, r, size)) {
    r[7] = uint8_t(function | 0x80);
    r[8] = error;
    size = 9;
  }
  put(r + 4, uint16_t(size - 6));
  return int(size);
}
}  // namespace forge
