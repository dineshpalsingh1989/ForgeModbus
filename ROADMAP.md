# What would make ForgeModbus worth choosing?

Version 0.2.0 serves all four Modbus tables with functions 1-6, 15 and 16. It is not yet
demonstrably faster or more reliable than established Modbus libraries. AsyncTCP
is a TCP transport, not a competing Modbus implementation; compare equivalent
Modbus workloads on top of each transport, not unrelated APIs.

## Current design choices

| Choice | Benefit | Tradeoff |
| --- | --- | --- |
| Fixed client slots and ADU buffers | Predictable application buffer storage: 2,080 bytes for four clients, plus bookkeeping | Space is reserved even when idle; lwIP still allocates its own memory |
| Cooperative nonblocking poll | Application controls when protocol work runs; no worker task unless `startTask()` is used | Application must poll frequently; no hard real-time guarantee |
| One response pending per client | A slow reader does not grow a library-owned response queue | Limits pipelined throughput |
| Independent protocol core | Protocol behavior can be tested without Wi-Fi or a board | Host tests cannot prove device/socket behavior |
| No third-party runtime dependencies | Smaller dependency surface to maintain | We own transport bugs, compatibility, and ongoing testing |

These choices are useful, but not claimed to be unique.

## Done in 0.2.0

- Functions 01, 02, 04, 05, 15 and 16; address offsets; unit ID filtering; read-only ranges.
- Read and write hooks, float and 32-bit helpers, flash persistence.
- Separate idle, incomplete-request and blocked-send deadlines, all configurable.
- TCP_NODELAY, TCP keepalive, IP allowlist, automatic listener recovery.
- Recursive lock, select()-based waiting, optional background task, more statistics.
- CI that runs host tests and compiles every example for each ESP32 chip.

## Next release priorities

1. Get CI green, then run the device smoke test on real hardware.
2. Add deterministic socket tests for partial writes, disconnects, resource exhaustion,
   timeout rollover and a slow reader. Run a real device memory soak.
3. Functions 22 (mask write), 23 (read/write multiple) and 43/14 (device identification).
4. Benchmark against pinned versions of established ESP32 Modbus TCP libraries.
5. Publish to the Arduino Library Manager and the PlatformIO registry.

## Evidence required for a better claim

Publish board model, Arduino core, competitor versions, Wi-Fi conditions, polling
interval, client count and complete scripts. Compare identical register operations:

- Median and p95 response latency, requests per second, and failure rate.
- Minimum free heap and largest free block during a long connection churn test.
- Behavior when one reader stalls while other clients continue.
- Recovery time after Wi-Fi loss and reconnection.

Publish regressions too. Favor a clear promise such as predictable resource use and
diagnosable failures over an unmeasured claim of being the fastest library.
