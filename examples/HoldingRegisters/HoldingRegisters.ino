// Minimal Modbus TCP server: 16 holding registers on port 502.
#include <WiFi.h>
#include <ForgeModbus.h>

uint16_t registers[16] = {};
forge::Server modbus(registers, 16);

void setup() {
  Serial.begin(115200);
  WiFi.begin("YOUR_WIFI_SSID", "YOUR_WIFI_PASSWORD");
  while (WiFi.status() != WL_CONNECTED) delay(250);
  Serial.print("Modbus TCP address: ");
  Serial.println(WiFi.localIP());
  // Register 0 is set by this sketch. Make it read-only for clients.
  modbus.readOnly(forge::Area::HoldingRegisters, 0, 1);
  if (!modbus.begin()) Serial.println("Listener not open yet; retrying in poll()");
}

void loop() {
  {
    forge::Server::Lock guard(modbus);
    registers[0] = uint16_t(millis() / 1000);  // uptime seconds, wraps at 65536
  }
  // Registers 1..15 can be written with functions 06 and 16.
  modbus.poll(10);  // waits up to 10 ms for network activity instead of spinning
}
