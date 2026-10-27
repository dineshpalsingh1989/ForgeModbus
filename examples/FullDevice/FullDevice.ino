// A complete device: all four Modbus tables, hooks, validation,
// read-only registers, float values and settings that survive a reboot.
//
// Map (protocol addresses, zero-based):
//   Coils 0-1             relay outputs
//   Discrete inputs 0     button (active low)
//   Input registers 0-1   temperature, float, ABCD order
//   Input registers 2-3   uptime seconds, uint32
//   Holding 0-1           setpoint, float 10.0 to 40.0, write both registers at once
//   Holding 2             mode, 0 to 2
//   Holding 10            firmware version, read-only
#include <WiFi.h>
#include <ForgeModbus.h>
#include <ForgeStore.h>

// Change these to pins that exist on your board.
const uint8_t relayPins[2] = {4, 5};
const uint8_t buttonPin = 0;  // BOOT button on many ESP32 and ESP32-S3 boards

uint8_t coils[forge::bitBytes(2)] = {};
uint8_t inputs[forge::bitBytes(1)] = {};
uint16_t inputRegisters[4] = {};
uint16_t holding[11] = {};

forge::Server modbus;
forge::Store store(&modbus);

float readTemperature() { return 20.0f + float(millis() / 1000 % 100) / 10.0f; }  // replace with your sensor

// Runs inside poll() with the server lock held, just before a client reads.
uint8_t beforeRead(const forge::Access& access, void*) {
  if (access.area == forge::Area::DiscreteInputs) {
    forge::setBit(inputs, 0, digitalRead(buttonPin) == LOW);
  } else if (access.area == forge::Area::InputRegisters) {
    forge::putFloat(inputRegisters, readTemperature());
    forge::putU32(inputRegisters + 2, millis() / 1000);
  }
  return forge::Ok;
}

// Rejects invalid values before anything is stored.
uint8_t beforeWrite(const forge::Access& access, void*) {
  if (access.area != forge::Area::HoldingRegisters) return forge::Ok;
  const uint32_t first = access.address, end = first + access.count;
  if (first < 2 && end > 0) {  // touches the setpoint
    if (first != 0 || end < 2) return forge::IllegalDataAddress;  // half a float is never valid
    const uint16_t raw[2] = {access.value(0), access.value(1)};
    const float setpoint = forge::getFloat(raw);
    if (!(setpoint >= 10.0f && setpoint <= 40.0f)) return forge::IllegalDataValue;
  }
  if (first <= 2 && end > 2 && access.value(uint16_t(2 - first)) > 2) return forge::IllegalDataValue;
  return forge::Ok;
}

void afterWrite(const forge::Access& access, void*) {
  if (access.area == forge::Area::Coils) {
    for (uint8_t i = 0; i < 2; ++i) digitalWrite(relayPins[i], forge::getBit(coils, i) ? HIGH : LOW);
  } else {
    store.changed();  // saved to flash a few seconds after the last write
    Serial.printf("Setpoint %.1f, mode %u\n", forge::getFloat(holding), holding[2]);
  }
}

void setup() {
  Serial.begin(115200);
  for (uint8_t pin : relayPins) pinMode(pin, OUTPUT);
  pinMode(buttonPin, INPUT_PULLUP);

  modbus.coils(coils, 2);
  modbus.discreteInputs(inputs, 1);
  modbus.inputRegisters(inputRegisters, 4);
  modbus.holdingRegisters(holding, 11);
  modbus.readOnly(forge::Area::HoldingRegisters, 10, 1);
  holding[10] = 0x0200;  // firmware 2.0

  forge::Hooks hooks;
  hooks.beforeRead = beforeRead;
  hooks.beforeWrite = beforeWrite;
  hooks.afterWrite = afterWrite;
  modbus.hooks(hooks);

  // Defaults, replaced by saved values when they exist.
  forge::putFloat(holding, 21.0f);
  holding[2] = 1;
  store.begin("device");
  store.add("settings", holding, 3);
  store.load();

  forge::Server::Config& config = modbus.config();
  config.idleTimeoutMs = 120000;  // drop clients silent for two minutes
  // modbus.unitId(1);                                            // answer unit 1 (and 255) only
  // modbus.allow(IPAddress(192, 168, 1, 0), IPAddress(255, 255, 255, 0));  // local subnet only

  WiFi.begin("YOUR_WIFI_SSID", "YOUR_WIFI_PASSWORD");
  while (WiFi.status() != WL_CONNECTED) delay(250);
  Serial.print("Modbus TCP address: ");
  Serial.println(WiFi.localIP());
  modbus.begin();
}

void loop() {
  modbus.poll(10);
  store.loop();
  static uint32_t lastReport = 0;
  if (millis() - lastReport >= 10000) {
    lastReport = millis();
    const forge::Server::Stats s = modbus.stats();
    Serial.printf("clients %u (peak %u), requests %lu, exceptions %lu, timeouts %lu/%lu/%lu\n",
                  s.activeClients, s.peakClients, (unsigned long)s.requests, (unsigned long)s.exceptions,
                  (unsigned long)s.idleTimeouts, (unsigned long)s.requestTimeouts, (unsigned long)s.sendTimeouts);
  }
}
