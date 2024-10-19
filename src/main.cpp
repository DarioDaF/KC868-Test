#include <Arduino.h>

#include <Wire.h>
#include <PCF8574_KC868.hpp>

#include <ETH.h>

#define MY_SDA 4
#define MY_SCL 5

PCF8574_KC868<2, 2> pcf8574s({ 0x22, 0x21 }, { 0x24, 0x25 }, 50);

template <typename _Tp, size_t _Nm>
constexpr size_t size(const _Tp (&/*__array*/)[_Nm]) noexcept { return _Nm; }

#pragma region ETH Stuff

static bool eth_connected = false;

void WiFiEvent(WiFiEvent_t event) {
  switch (event) {
    case ARDUINO_EVENT_ETH_START:
      Serial.println("ETH Started");
      //set eth hostname here
      ETH.setHostname("esp32-ethernet");
      break;
    case ARDUINO_EVENT_ETH_CONNECTED:
      Serial.println("ETH Connected");
      break;
    case ARDUINO_EVENT_ETH_GOT_IP:
      Serial.print("ETH MAC: ");
      Serial.print(ETH.macAddress());
      Serial.print(", IPv4: ");
      Serial.print(ETH.localIP());
      if (ETH.fullDuplex()) {
        Serial.print(", FULL_DUPLEX");
      }
      Serial.print(", ");
      Serial.print(ETH.linkSpeed());
      Serial.println("Mbps");
      eth_connected = true;
      break;
    case ARDUINO_EVENT_ETH_DISCONNECTED:
      Serial.println("ETH Disconnected");
      eth_connected = false;
      break;
    case ARDUINO_EVENT_ETH_STOP:
      Serial.println("ETH Stopped");
      eth_connected = false;
      break;
    default:
      break;
  }
}

void testClient(const char* host, uint16_t port) {
  Serial.print("\nconnecting to ");
  Serial.println(host);

  WiFiClient client;
  if (!client.connect(host, port)) {
    Serial.println("connection failed");
    return;
  }
  client.printf("GET / HTTP/1.1\r\nHost: %s\r\n\r\n", host);
  while (client.connected() && !client.available());
  while (client.available()) {
    Serial.write(client.read());
  }

  Serial.println("closing connection\n");
  client.stop();
}

#pragma endregion ETH Stuff

void setup() {
  Serial.begin(115200);
  delay(1000);

  WiFi.onEvent(WiFiEvent);

  Serial.print("Init I2C ");
  if (Wire.begin(MY_SDA, MY_SCL, 100000UL)) { // 100kHz
		Serial.println("OK");
	} else {
		Serial.println("KO");
	}

	Serial.print("Init PCF8574 ");
	if (pcf8574s.begin()) {
		Serial.println("OK");
	} else {
		Serial.println("KO");
	}

	Serial.print("Init ETH ");
  if (
    ETH.begin(
      0,
      -1,
      ETH_PHY_MDC, // 23
      ETH_PHY_MDIO, // 18
      eth_phy_type_t::ETH_PHY_LAN8720,
      eth_clock_mode_t::ETH_CLOCK_GPIO17_OUT, // 17
      true // MAC address from EFUSE
    )
  ) {
		Serial.println("OK");
	} else {
		Serial.println("KO");
	}

	Serial.print("Config ETH ");
  if (
    ETH.config(
      { 192, 168, 1, 110 },
      { 192, 168, 1, 1 },
      { 255, 255, 255, 0 },
      { 8, 8, 8, 8 },
      { 0, 0, 0, 0 }
    )
  ) {
		Serial.println("OK");
	} else {
		Serial.println("KO");
	}
}

unsigned long lastInputPrint = millis();
unsigned long lastEthSend = millis();

void loop() {
  unsigned long now = millis();

  if (pcf8574s.updateInput() >= 0) { // Was executed (could have an error if > 0)
    static_assert(size(pcf8574s.ins) == size(pcf8574s.outs));
    if (memcmp(pcf8574s.outs, pcf8574s.ins, sizeof(pcf8574s.ins)) != 0) {
      memcpy(pcf8574s.outs, pcf8574s.ins, sizeof(pcf8574s.ins));
      pcf8574s.flushOutput();
    }
  }

  if (now - lastInputPrint > 1000) {
    lastInputPrint = now;
    for (int i = 0; i < pcf8574s.inputs(); ++i) {
      Serial.print(pcf8574s.digitalRead(i) ? "H" : "L");
    }
    Serial.println();
  }

  if (now - lastEthSend > 10000) {
    lastEthSend = now;
    if (eth_connected) {
      testClient("google.com", 80);
    } else {
      Serial.println("ETH not connected");
    }
  }

  //delay(10);
}
