#include "ForgeProtocol.h"
#include "ForgeValues.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <initializer_list>
#include <vector>

using forge::Area;
using forge::Protocol;
using Bytes = std::vector<uint8_t>;

// Builds an ADU from unit id, function code and PDU data. Transaction id 0x1234.
static Bytes adu(uint8_t unit, uint8_t function, std::initializer_list<uint8_t> data) {
  Bytes q = {0x12, 0x34, 0, 0, 0, uint8_t(2 + data.size()), unit, function};
  q.insert(q.end(), data.begin(), data.end());
  return q;
}
static Bytes call(Protocol& p, const Bytes& q, int* result = nullptr) {
  uint8_t r[Protocol::MaxAdu] = {};
  const int size = p.respond(q.data(), q.size(), r);
  if (result) *result = size;
  if (size <= 0) return {};
  assert(size_t(size) == 6u + (r[4] << 8 | r[5]));  // MBAP length matches
  assert(r[0] == q[0] && r[1] == q[1] && r[6] == q[6]);
  return Bytes(r, r + size);
}
static uint8_t exceptionOf(Protocol& p, const Bytes& q) {
  const Bytes r = call(p, q);
  assert(r.size() == 9 && r[7] == (q[7] | 0x80));
  return r[8];
}
static Bytes pdu(const Bytes& r) { return Bytes(r.begin() + 7, r.end()); }

static void readHolding() {
  uint16_t registers[125] = {0x1234, 0xabcd};
  Protocol p(registers, 125);
  assert(pdu(call(p, adu(1, 3, {0, 0, 0, 2}))) == Bytes({3, 4, 0x12, 0x34, 0xab, 0xcd}));
  const Bytes all = call(p, adu(1, 3, {0, 0, 0, 125}));
  assert(all.size() == 259 && all[8] == 250);
  assert(exceptionOf(p, adu(1, 3, {0, 0, 0, 126})) == forge::IllegalDataValue);
  assert(exceptionOf(p, adu(1, 3, {0, 0, 0, 0})) == forge::IllegalDataValue);
  assert(exceptionOf(p, adu(1, 3, {0, 124, 0, 2})) == forge::IllegalDataAddress);
  assert(exceptionOf(p, adu(1, 3, {0, 0, 0})) == forge::IllegalDataValue);  // short PDU
  assert(exceptionOf(p, adu(1, 4, {0, 0, 0, 1})) == forge::IllegalDataAddress);  // no input table
}

static void writeHolding() {
  uint16_t registers[10] = {};
  Protocol p(registers, 10);
  const Bytes single = adu(1, 6, {0, 1, 0x55, 0xaa});
  assert(call(p, single) == single && registers[1] == 0x55aa);
  assert(exceptionOf(p, adu(1, 6, {0xff, 0xff, 0, 1})) == forge::IllegalDataAddress);
  const Bytes multi = adu(1, 16, {0, 2, 0, 3, 6, 0, 1, 0, 2, 0xff, 0xfe});
  assert(pdu(call(p, multi)) == Bytes({16, 0, 2, 0, 3}));
  assert(registers[2] == 1 && registers[3] == 2 && registers[4] == 0xfffe);
  assert(exceptionOf(p, adu(1, 16, {0, 0, 0, 2, 3, 0, 1, 0})) == forge::IllegalDataValue);  // byte count
  assert(exceptionOf(p, adu(1, 16, {0, 0, 0, 1, 2, 0, 1, 0})) == forge::IllegalDataValue);  // extra byte
  assert(exceptionOf(p, adu(1, 16, {0, 9, 0, 2, 4, 0, 1, 0, 2})) == forge::IllegalDataAddress);
  assert(registers[9] == 0);
  assert(exceptionOf(p, adu(1, 16, {0, 0, 0, 0, 0})) == forge::IllegalDataValue);

  uint16_t big[123] = {};
  Protocol maximum(big, 123);
  Bytes all = adu(1, 16, {0, 0, 0, 123, 246});
  all.resize(all.size() + 246, 0x11);
  all[5] = uint8_t(all.size() - 6);
  assert(all.size() == 259 && call(maximum, all).size() == 12 && big[122] == 0x1111);
}

static void inputRegisters() {
  uint16_t inputs[4] = {7, 8, 9, 10};
  Protocol p;
  p.inputRegisters(inputs, 4, 100);
  assert(pdu(call(p, adu(1, 4, {0, 101, 0, 2}))) == Bytes({4, 4, 0, 8, 0, 9}));
  assert(exceptionOf(p, adu(1, 4, {0, 99, 0, 1})) == forge::IllegalDataAddress);
  assert(exceptionOf(p, adu(1, 4, {0, 103, 0, 2})) == forge::IllegalDataAddress);
  assert(exceptionOf(p, adu(1, 6, {0, 100, 0, 1})) == forge::IllegalDataAddress);  // not writable
}

static void coilsAndInputs() {
  uint8_t coils[2] = {0x05, 0x80};  // coils 0, 2 and 15 on
  uint8_t discrete[1] = {0x03};
  Protocol p;
  p.coils(coils, 16);
  p.discreteInputs(discrete, 3, 10);
  assert(pdu(call(p, adu(1, 1, {0, 0, 0, 16}))) == Bytes({1, 2, 0x05, 0x80}));
  assert(pdu(call(p, adu(1, 1, {0, 1, 0, 2}))) == Bytes({1, 1, 0x02}));  // shifted, padded with zeros
  assert(pdu(call(p, adu(1, 2, {0, 10, 0, 3}))) == Bytes({2, 1, 0x03}));
  assert(exceptionOf(p, adu(1, 1, {0, 0, 0, 17})) == forge::IllegalDataAddress);
  assert(exceptionOf(p, adu(1, 1, {0, 0, 0x07, 0xd1})) == forge::IllegalDataValue);  // 2001 coils

  const Bytes on = adu(1, 5, {0, 3, 0xff, 0});
  assert(call(p, on) == on && coils[0] == 0x0d);
  assert(call(p, adu(1, 5, {0, 0, 0, 0})).size() == 12 && coils[0] == 0x0c);
  assert(exceptionOf(p, adu(1, 5, {0, 0, 0x12, 0x34})) == forge::IllegalDataValue);
  assert(exceptionOf(p, adu(1, 5, {0, 16, 0xff, 0})) == forge::IllegalDataAddress);

  // Write 10 coils from 4: values 1,1,0,0,1,1,0,0, 1,0 (LSB first)
  assert(pdu(call(p, adu(1, 15, {0, 4, 0, 10, 2, 0x33, 0x01}))) == Bytes({15, 0, 4, 0, 10}));
  assert(coils[0] == 0x3c && coils[1] == 0x93);  // coil 15 untouched
  assert(exceptionOf(p, adu(1, 15, {0, 0, 0, 9, 1, 0xff})) == forge::IllegalDataValue);
  assert(exceptionOf(p, adu(1, 15, {0, 10, 0, 8, 1, 0xff})) == forge::IllegalDataAddress);
  assert(exceptionOf(p, adu(1, 15, {0, 0, 0, 0, 0})) == forge::IllegalDataValue);
}

static void readOnlyRanges() {
  uint16_t registers[10] = {};
  uint8_t coils[1] = {};
  Protocol p(registers, 10);
  p.coils(coils, 8);
  assert(p.readOnly(Area::HoldingRegisters, 4, 2));
  assert(p.readOnly(Area::Coils, 7, 1));
  assert(!p.readOnly(Area::Coils, 0, 0));
  assert(exceptionOf(p, adu(1, 6, {0, 5, 0, 1})) == forge::IllegalDataAddress);
  assert(exceptionOf(p, adu(1, 16, {0, 3, 0, 2, 4, 0, 1, 0, 1})) == forge::IllegalDataAddress);
  assert(registers[3] == 0);  // nothing written by the rejected request
  assert(call(p, adu(1, 16, {0, 2, 0, 2, 4, 0, 1, 0, 1})).size() == 12);
  assert(call(p, adu(1, 3, {0, 4, 0, 2})).size() == 13);  // still readable
  assert(exceptionOf(p, adu(1, 5, {0, 7, 0xff, 0})) == forge::IllegalDataAddress);
  assert(call(p, adu(1, 5, {0, 6, 0xff, 0})).size() == 12 && coils[0] == 0x40);
  for (int i = 2; i < 4; ++i) assert(p.readOnly(Area::InputRegisters, 0, 1));
  assert(!p.readOnly(Area::InputRegisters, 0, 1));  // all four slots used
}

struct Recorder {
  int reads = 0, checks = 0, writes = 0;
  forge::Access last = {};
  uint16_t lastValue = 0;
  uint8_t reject = 0;
};
static uint8_t beforeRead(const forge::Access& a, void* c) {
  Recorder& r = *static_cast<Recorder*>(c);
  ++r.reads;
  r.last = a;
  return 0;
}
static uint8_t beforeWrite(const forge::Access& a, void* c) {
  Recorder& r = *static_cast<Recorder*>(c);
  ++r.checks;
  r.last = a;
  r.lastValue = a.area == Area::Coils ? a.bitAt(a.count - 1) : a.value(a.count - 1);
  return r.reject;
}
static void afterWrite(const forge::Access& a, void* c) {
  Recorder& r = *static_cast<Recorder*>(c);
  ++r.writes;
  r.last = a;
}

static void hooks() {
  uint16_t registers[10] = {};
  uint8_t coils[2] = {};
  Recorder rec;
  Protocol p(registers, 10);
  p.coils(coils, 16);
  forge::Hooks h;
  h.beforeRead = beforeRead;
  h.beforeWrite = beforeWrite;
  h.afterWrite = afterWrite;
  h.context = &rec;
  p.hooks(h);

  call(p, adu(9, 3, {0, 2, 0, 3}));
  assert(rec.reads == 1 && rec.last.area == Area::HoldingRegisters && rec.last.unit == 9 &&
         rec.last.address == 2 && rec.last.count == 3);
  call(p, adu(1, 16, {0, 1, 0, 2, 4, 0, 7, 0x01, 0x2c}));
  assert(rec.checks == 1 && rec.writes == 1 && rec.lastValue == 300 && rec.last.count == 2);
  call(p, adu(1, 5, {0, 9, 0xff, 0}));
  assert(rec.checks == 2 && rec.lastValue == 1 && rec.last.area == Area::Coils && rec.last.address == 9);
  call(p, adu(1, 15, {0, 0, 0, 3, 1, 0x03}));
  assert(rec.checks == 3 && rec.lastValue == 0 && coils[0] == 0x03);

  rec.reject = forge::IllegalDataValue;  // e.g. setpoint out of range
  assert(exceptionOf(p, adu(1, 6, {0, 0, 0x27, 0x10})) == forge::IllegalDataValue);
  assert(registers[0] == 0 && rec.writes == 3);  // rejected write is not stored or reported
  rec.reject = 0;
  call(p, adu(1, 3, {0, 9, 0, 2}));  // unmapped: hook never sees invalid requests
  assert(rec.reads == 1);
}

static void framingAndUnits() {
  uint16_t registers[4] = {};
  Protocol p(registers, 4);
  int result = 0;
  Bytes q = adu(1, 3, {0, 0, 0, 1});
  q[3] = 1;  // protocol id must be zero
  call(p, q, &result);
  assert(result == Protocol::Close);
  q[3] = 0;
  uint8_t r[Protocol::MaxAdu];
  assert(p.respond(q.data(), 5, r) == Protocol::Close);
  assert(p.respond(q.data(), q.size() - 1, r) == Protocol::Close);  // length mismatch
  assert(exceptionOf(p, adu(1, 0x41, {})) == forge::IllegalFunction);
  call(p, adu(1, 0x83, {0, 0, 0, 1}), &result);
  assert(result == Protocol::Ignore);  // exception codes are not requests

  p.unitId(5);
  call(p, adu(1, 3, {0, 0, 0, 1}), &result);
  assert(result == Protocol::Ignore);
  assert(call(p, adu(5, 3, {0, 0, 0, 1})).size() == 11);
  assert(call(p, adu(255, 3, {0, 0, 0, 1})).size() == 11);

  Bytes h = {0, 0, 0, 0, 1, 0};
  assert(forge::frameSize(h.data()) == 0);
  h[4] = 0; h[5] = 1;
  assert(forge::frameSize(h.data()) == 0);
  h[5] = 254;
  assert(forge::frameSize(h.data()) == 260);

  Protocol empty;
  assert(exceptionOf(empty, adu(1, 3, {0, 0, 0, 1})) == forge::IllegalDataAddress);
  assert(exceptionOf(empty, adu(1, 1, {0, 0, 0, 1})) == forge::IllegalDataAddress);
}

static void values() {
  using forge::WordOrder;
  uint16_t r[2];
  forge::putU32(r, 0x11223344);
  assert(r[0] == 0x1122 && r[1] == 0x3344);
  forge::putU32(r, 0x11223344, WordOrder::CDAB);
  assert(r[0] == 0x3344 && r[1] == 0x1122);
  forge::putU32(r, 0x11223344, WordOrder::BADC);
  assert(r[0] == 0x2211 && r[1] == 0x4433);
  forge::putU32(r, 0x11223344, WordOrder::DCBA);
  assert(r[0] == 0x4433 && r[1] == 0x2211);
  for (WordOrder o : {WordOrder::ABCD, WordOrder::CDAB, WordOrder::BADC, WordOrder::DCBA}) {
    forge::putU32(r, 0xdeadbeef, o);
    assert(forge::getU32(r, o) == 0xdeadbeef);
    forge::putI32(r, -5, o);
    assert(forge::getI32(r, o) == -5);
    forge::putFloat(r, 21.5f, o);
    assert(forge::getFloat(r, o) == 21.5f);
  }
  forge::putFloat(r, 1.0f);
  assert(r[0] == 0x3f80 && r[1] == 0);
  uint8_t bits[forge::bitBytes(10)] = {};
  static_assert(sizeof(bits) == 2, "two bytes hold ten bits");
  forge::setBit(bits, 9, true);
  forge::setBit(bits, 0, true);
  forge::setBit(bits, 0, false);
  assert(bits[0] == 0 && bits[1] == 2 && forge::getBit(bits, 9) && !forge::getBit(bits, 8));
}

int main() {
  readHolding();
  writeHolding();
  inputRegisters();
  coilsAndInputs();
  readOnlyRanges();
  hooks();
  framingAndUnits();
  values();
  puts("Protocol tests passed");
}
