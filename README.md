# ForgeModbus

A Modbus TCP server library for ESP32 Arduino, version 0.2.0. It works over Wi-Fi or
Ethernet using the ESP32's built-in lwIP sockets. There is no AsyncTCP or third-party
Modbus dependency.

## Features

- **All four Modbus tables.** Coils, discrete inputs, input registers and holding registers,
  each starting at any address.
- **Function codes 01, 02, 03, 04, 05, 06, 15 and 16**, with the exception responses the
  specification requires.
- **Hooks.** Refresh values just before a read, validate or reject a write, and react after a write.
- **Read-only ranges** inside the coil and holding register tables.
- **Unit ID filtering** for use behind gateways.
- **32-bit and float helpers** in all four word orders (ABCD, CDAB, BADC, DCBA).
- **Flash persistence** of chosen tables with delayed, batched saves.
- **IP allowlist** with subnet masks.
- **Separate timeouts** for idle clients, incomplete requests and stalled sends. TCP keepalive
  detects clients that vanish.
- **TCP_NODELAY**, so replies leave immediately.
- **Network recovery.** A failed listener is reopened automatically.
- **Thread safety.** A recursive lock is shared with `poll()`, and an optional background task
  can run the server.
- **Predictable memory.** Four client slots by default, with fixed 260-byte buffers each.

Supported chips: ESP32, ESP32-S2, S3, C3, C5, C6 and P4. The ESP32-H2 has no Wi-Fi or
Ethernet, and the ESP8266 is a different platform.

## Install

Arduino IDE: copy this folder to your sketchbook's `libraries/ForgeModbus` folder.
PlatformIO: add the repository URL to `lib_deps`.

## Quick start

```cpp
#include <WiFi.h>
#include <ForgeModbus.h>

uint16_t holding[16] = {};
forge::Server modbus(holding, 16);

void setup() {
  WiFi.begin("ssid", "password");
  while (WiFi.status() != WL_CONNECTED) delay(250);
  modbus.begin();          // port 502
}

void loop() {
  modbus.poll(10);         // waits up to 10 ms for network activity
}
```

Addresses are zero-based protocol addresses. Many clients show holding register 0 as 40001.

## Examples

| Example | Shows |
| --- | --- |
| HoldingRegisters | The smallest server, with a read-only register. |
| FullDevice | All tables, hooks, validation, floats, persistence, timeouts and statistics. |
| BackgroundTask | The server in its own FreeRTOS task, with locking from `loop()`. |
| Ethernet | A WT32-ETH01 board with a LAN8720 PHY. |

## Tables

```cpp
uint8_t coils[forge::bitBytes(20)];   // bits are packed, 8 per byte
modbus.coils(coils, 20);              // addresses 0..19
modbus.inputRegisters(inputs, 10, 100); // addresses 100..109
modbus.readOnly(forge::Area::HoldingRegisters, 10, 2);
modbus.unitId(1);                     // answer unit 1 and 255, ignore others
```

Use `forge::getBit()` and `forge::setBit()` for coils and discrete inputs. Use
`forge::putFloat()`, `getFloat()`, `putU32()` and `getU32()` for two-register values.

## Hooks

```cpp
uint8_t check(const forge::Access& a, void*) {
  if (a.area == forge::Area::HoldingRegisters && a.address == 0 && a.value(0) > 100)
    return forge::IllegalDataValue;  // the master receives exception 03
  return forge::Ok;
}
forge::Hooks hooks;
hooks.beforeWrite = check;
modbus.hooks(hooks);
```

`beforeRead` runs before a read. `beforeWrite` can reject a write before anything is
stored. `afterWrite` runs once the values are in the table. Hooks run inside `poll()`
with the server lock held. Keep them short and do not call `end()` from a hook.
Captureless lambdas work as hooks. Pass state through `hooks.context`.

## Configuration

Set these before `begin()`:

| Setting | Default | Meaning |
| --- | --- | --- |
| `port` | 502 | TCP port. |
| `idleTimeoutMs` | 0 | Close clients idle this long. 0 keeps them connected. |
| `requestTimeoutMs` | 5000 | A started request must arrive completely in this time. |
| `sendTimeoutMs` | 5000 | A reply must be fully sent in this time. |
| `keepAliveSec` | 30 | TCP keepalive idle time. 0 disables it. |
| `noDelay` | true | Send each reply immediately. |
| `retryMs` | 1000 | Wait between attempts to reopen a failed listener. |

```cpp
modbus.config().idleTimeoutMs = 60000;
modbus.allow(IPAddress(192, 168, 1, 0), IPAddress(255, 255, 255, 0));
```

Define `FORGE_MAX_CLIENTS` before including the library to change the client limit.

## Threading

`poll()` holds the server lock while it reads tables and runs hooks. Any other task that
touches the tables must hold the lock too:

```cpp
{
  forge::Server::Lock guard(modbus);
  inputs[0] = reading;
}
```

Call `startTask()` to run the server in its own task instead of calling `poll()`.
Do not call `end()` while holding the lock.

## Persistence

```cpp
#include <ForgeStore.h>
forge::Store store(&modbus);
store.begin("device");
store.add("settings", holding, 3);
store.load();          // restore saved values, if any
// In afterWrite: store.changed();
// In loop():     store.loop();
```

Saves happen `store.delayMs` (5000 by default) after the first change, so a burst
of writes costs one flash write.

## Statistics

`modbus.stats()` returns counters for accepted, rejected and denied clients, requests,
exceptions, ignored requests, invalid frames, each kind of timeout, socket errors,
listener restarts, and active and peak clients.

## Security

Modbus TCP has no authentication or encryption. Use it on a trusted network. The
allowlist limits who can connect but does not protect against address spoofing.

## Verification

Status: the protocol tests and examples have not yet been run or compiled on this PC.
It has no host C++ compiler, and the ESP32 core download keeps being aborted by the
network. The CI workflow builds and runs the host tests and compiles every example
for each supported chip on each push.

Protocol tests on a host with CMake:

```sh
cmake -S . -B build
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

Compile all examples on Windows with a workspace-local Arduino CLI:

```powershell
.\tools\build-esp32.ps1 -Install                 # first time
.\tools\build-esp32.ps1 -Board esp32:esp32:esp32s3
```

Device smoke test against the HoldingRegisters example. It writes registers 1 to 3:

```sh
node tools/smoke-test.mjs ESP32_IP 502
```

Hardware acceptance still required: test with real masters, exhaust the client slots,
interrupt Wi-Fi, test a slow reader, and run a long memory soak.

## Publishing

ForgeModbus is released under the MIT License; see `LICENSE`. To list the library
in the Arduino Library Manager, tag a release and submit the repository URL to
[arduino/library-registry](https://github.com/arduino/library-registry). PlatformIO
picks it up from `library.json`.

## References

- [Modbus application protocol V1.1b3](https://www.modbus.org/file/secure/modbusprotocolspecification.pdf)
- [Modbus TCP implementation guide V1.0b](https://www.modbus.org/file/secure/messagingimplementationguide.pdf)
