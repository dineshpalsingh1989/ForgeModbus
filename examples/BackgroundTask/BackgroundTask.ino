// Runs the server in its own FreeRTOS task. loop() stays free for other work
// and uses Server::Lock whenever it touches the register tables.
#include <WiFi.h>
#include <ForgeModbus.h>

uint16_t inputs[8] = {};
uint16_t holding[8] = {};
forge::Server modbus;

void setup() {
  Serial.begin(115200);
  modbus.inputRegisters(inputs, 8);
  modbus.holdingRegisters(holding, 8);

  WiFi.begin("YOUR_WIFI_SSID", "YOUR_WIFI_PASSWORD");
  while (WiFi.status() != WL_CONNECTED) delay(250);
  Serial.print("Modbus TCP address: ");
  Serial.println(WiFi.localIP());
  modbus.begin();
  if (!modbus.startTask()) Serial.println("Unable to start Modbus task");
}

void loop() {
  const uint16_t reading = uint16_t(ESP.getFreeHeap() / 1024);  // slow work, such as sensors, runs unlocked
  uint16_t command;
  {
    forge::Server::Lock guard(modbus);  // keep locked sections short
    inputs[0] = reading;
    inputs[1] = uint16_t(millis() / 1000);
    command = holding[0];
  }
  static uint16_t lastCommand = 0;
  if (command != lastCommand) {
    lastCommand = command;
    Serial.printf("Holding register 0 changed to %u\n", command);
  }
  delay(100);
}
