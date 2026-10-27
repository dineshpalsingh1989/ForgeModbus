// Modbus TCP over wired Ethernet on a WT32-ETH01 (LAN8720 PHY).
// For other boards, change the PHY settings to match your schematic.
// These pins and clock settings are for the original ESP32. On other chips,
// use the ETH.begin() form for your PHY, such as SPI Ethernet (W5500).
#include <ETH.h>
#include <ForgeModbus.h>

uint16_t registers[16] = {};
forge::Server modbus(registers, 16);

void setup() {
  Serial.begin(115200);
  // PHY type, PHY address, MDC, MDIO, power pin, clock mode.
  if (!ETH.begin(ETH_PHY_LAN8720, 1, 23, 18, 16, ETH_CLOCK_GPIO0_IN)) Serial.println("Ethernet start failed");
  while (!ETH.hasIP()) delay(250);
  Serial.print("Modbus TCP address: ");
  Serial.println(ETH.localIP());
  modbus.begin();  // the listener also reopens itself if the network fails
}

void loop() {
  {
    forge::Server::Lock guard(modbus);
    registers[0] = uint16_t(millis() / 1000);
  }
  modbus.poll(10);
}
