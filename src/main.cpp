#include <Arduino.h>
#include <Wire.h>
#include <PCF8574_KC868.hpp>
#include <SPI.h>
#include <ETH.h>

using millis_t = unsigned long;

millis_t lastInputPrint = 0;
millis_t lastEthSend = 0;
PCF8574_KC868<2, 2> pcf8574s({ 0x22, 0x21 }, { 0x24, 0x25 }, 50);

template <typename _Tp, size_t _Nm>
constexpr size_t size(const _Tp (&/*__array*/)[_Nm]) noexcept { return _Nm; }

#pragma region PIN DEFINITIONS

#define _SDA GPIO_NUM_4
#define _SCL GPIO_NUM_5
#define TX_RS485 GPIO_NUM_13
#define RX_RS485 GPIO_NUM_16
#define TX_433M GPIO_NUM_15
#define RX_433M GPIO_NUM_2
// HT1, HT2 and HT3
constexpr uint8_t pinTemperature[] = { GPIO_NUM_32, GPIO_NUM_33, GPIO_NUM_14 };
// INA1, INA2, INA3 and INA4
// pinAnalog[0] and pinAnalog[1] (0..20mA) - pinAnalog[2] and pinAnalog[3] (0..3,3V)
constexpr uint8_t pinAnalog[] = { GPIO_NUM_36, GPIO_NUM_34, GPIO_NUM_35, GPIO_NUM_39 };

#pragma endregion PIN DEFINITIONS

#pragma region ETHERNET

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

#pragma endregion ETHERNET

#pragma region ANALOGS

uint16_t analog[size(pinAnalog)];
constexpr millis_t updateAnalogInterval = 100;
millis_t lastAnalogUpdate = 0;

void analogSetup() {
  analogSetAttenuation(ADC_11db);
  analogReadResolution(12);
}

void analogUpdate(millis_t now) {
  if(now - lastAnalogUpdate >= updateAnalogInterval) {
    for(size_t i = 0; i < size(analog); ++i)
      analog[i] = analogRead(pinAnalog[i]);
    lastAnalogUpdate = now;
  }
}

#pragma endregion ANALOGS

#pragma region MODBUS

#include <ModbusServerTCPasync.h>

// Create server
ModbusServerTCPasync MBserver;

uint16_t hold_registers[32];

// Server function to handle FC01=READ_COIL or FC02=READ_DISCR_INPUT
ModbusMessage FC01(ModbusMessage request) {
  ModbusMessage response;      // The Modbus message we are going to give back
  uint16_t start = 0;          // Start address
  uint16_t count = 0;          // # of coils requested
  request.get(2, start);       // read address from request
  request.get(4, count);       // read # of words from request

  if ((start + count) > pcf8574s.outputs()) {
    response.setError(request.getServerID(), request.getFunctionCode(), ILLEGAL_DATA_ADDRESS);
  } else {
    std::vector<uint8_t> res((count + 7) / 8, 0x00);
    for (uint8_t i = 0; i < count; ++i) {
      if (pcf8574s.readOutput(i + start)) {
        res[i / 8] |= _BV(i % 8);
      }
    }
    response.add(request.getServerID(), request.getFunctionCode(), (uint8_t)res.size(), res);
  }
  return response;
}

// Server function to handle FC02=READ_DISCR_INPUT
ModbusMessage FC02(ModbusMessage request) {
  ModbusMessage response;      // The Modbus message we are going to give back
  uint16_t start = 0;          // Start address
  uint16_t count = 0;          // # of coils requested
  request.get(2, start);       // read address from request
  request.get(4, count);       // read # of words from request

  if ((start + count) > pcf8574s.inputs()) {
    response.setError(request.getServerID(), request.getFunctionCode(), ILLEGAL_DATA_ADDRESS);
  } else {
    std::vector<uint8_t> res((count + 7) / 8, 0x00);
    for (uint8_t i = 0; i < count; ++i) {
      if (pcf8574s.readInput(i + start)) {
        res[i / 8] |= _BV(i % 8);
      }
    }
    response.add(request.getServerID(), request.getFunctionCode(), (uint8_t)res.size(), res);
  }
  return response;
}

// Server function to handle FC05=WRITE_COIL
ModbusMessage FC05(ModbusMessage request) {
  ModbusMessage response;
  // Request parameters are coil number and 0x0000 (OFF) or 0xFF00 (ON)
  uint16_t start = 0;
  uint16_t state = 0;
  request.get(2, start, state);

  if(start < pcf8574s.outputs()) {
    if((state == 0x0000) || (state == 0xFF00)) {
      pcf8574s.writeOutput(start, state == 0xFF00);
      pcf8574s.flushOutput();
      response = ECHO_RESPONSE;
    } else {
      response.setError(request.getServerID(), request.getFunctionCode(), ILLEGAL_DATA_VALUE);
    }
  } else {
    response.setError(request.getServerID(), request.getFunctionCode(), ILLEGAL_DATA_ADDRESS);
  }
  return response;
}

// Server function to handle FC0F=WRITE_MULT_COILS
ModbusMessage FC0F(ModbusMessage request) {
  ModbusMessage response;      // The Modbus message we are going to give back
  uint16_t start = 0;
  uint16_t numCoils = 0;
  uint8_t numBytes = 0;
  uint16_t offset = 2;    // Parameters start after serverID and FC
  offset = request.get(offset, start, numCoils, numBytes);

  if (numCoils > numBytes * 8) {
    response.setError(request.getServerID(), request.getFunctionCode(), ILLEGAL_DATA_VALUE);
  } else if (start + numCoils > pcf8574s.outputs()) {
    response.setError(request.getServerID(), request.getFunctionCode(), ILLEGAL_DATA_ADDRESS);
    return response;
  } else {
    vector<uint8_t> coilset;
    request.get(offset, coilset, numBytes);
    for (uint8_t i = 0; i < numCoils; ++i)
      pcf8574s.writeOutput(i + start, coilset[i / 8] & _BV(i % 8));
    pcf8574s.flushOutput();
    response.add(request.getServerID(), request.getFunctionCode(), start, numCoils);
  }
  return response;
}

// Server function to handle FC03=READ_HOLD_REGISTER
ModbusMessage FC03(ModbusMessage request) {
  ModbusMessage response;      // The Modbus message we are going to give back
  uint16_t start = 0;          // Start address
  uint16_t numWords = 0;       // # of words requested
  request.get(2, start);       // read address from request
  request.get(4, numWords);    // read # of words from request

  if((start + numWords) > size(hold_registers)) {
    response.setError(request.getServerID(), request.getFunctionCode(), ILLEGAL_DATA_ADDRESS);
  } else {
    response.add(request.getServerID(), request.getFunctionCode(), (uint8_t)(numWords * 2));
    for(uint8_t i = 0; i < numWords; ++i)
      response.add((uint16_t)(hold_registers[i + start]));
  }
  return response;
}

// Server function to handle FC04=READ_INPUT_REGISTER
ModbusMessage FC04(ModbusMessage request) {
  ModbusMessage response;      // The Modbus message we are going to give back
  uint16_t start = 0;          // Start address
  uint16_t numWords = 0;       // # of words requested
  request.get(2, start);       // read address from request
  request.get(4, numWords);    // read # of words from request

  if ((start + numWords) > size(analog)) {
    response.setError(request.getServerID(), request.getFunctionCode(), ILLEGAL_DATA_ADDRESS);
  } else {
    response.add(request.getServerID(), request.getFunctionCode(), (uint8_t)(numWords * 2));
    for (uint8_t i = 0; i < numWords; ++i)
      response.add((uint16_t)(analog[i + start]));
  }
  return response;
}

// Server function to handle FC06=WRITE_HOLD_REGISTER and FC10=WRITE_MULT_REGISTERS
ModbusMessage FC06(ModbusMessage request) {
  ModbusMessage response;      // The Modbus message we are going to give back
  uint16_t addr = 0;           // Start address
  uint16_t value = 0;          // # of words requested
  request.get(2, addr);        // read address from request
  request.get(4, value);       // read # of words from request

  if(addr >= size(hold_registers)) {
    response.setError(request.getServerID(), request.getFunctionCode(), ILLEGAL_DATA_ADDRESS);
  } else {
    hold_registers[addr] = value;
    response = ECHO_RESPONSE;
  }
  return response;
}


// Server function to handle FC06=WRITE_HOLD_REGISTER and FC10=WRITE_MULT_REGISTERS
ModbusMessage FC10(ModbusMessage request) {
  ModbusMessage response;      // The Modbus message we are going to give back
  uint16_t start = 0;          // Start address
  uint16_t numWords = 0;       // # of words requested
  uint8_t numBytes = 0;
  uint16_t offset = request.get(2, start, numWords, numBytes);

  if ((start + numWords) > size(hold_registers)) {
    response.setError(request.getServerID(), request.getFunctionCode(), ILLEGAL_DATA_ADDRESS);
  } else {
    for(size_t i = 0; i < numWords; ++i)
      offset = request.get(offset, hold_registers[i + start]);
    response.add(request.getServerID(), request.getFunctionCode(), start, numWords);
  }
  return response;
}

void mbSetup() {
  //for (uint16_t i = 0; i < size(hold_registers); ++i) {
  //  hold_registers[i] = (i * 2) << 8 | ((i * 2) + 1);
  //}

  // Define and start RTU server
  MBserver.registerWorker(1, READ_COIL, &FC01);               // FC=0x01 for serverID=1
  MBserver.registerWorker(1, READ_DISCR_INPUT, &FC02);        // FC=0x02 for serverID=1
  MBserver.registerWorker(1, READ_HOLD_REGISTER, &FC03);      // FC=0x03 for serverID=1
  MBserver.registerWorker(1, READ_INPUT_REGISTER, &FC04);     // FC=0x04 for serverID=1
  MBserver.registerWorker(1, WRITE_COIL, &FC05);              // FC=0x05 for serverID=1
  MBserver.registerWorker(1, WRITE_MULT_COILS, &FC0F);        // FC=0x0F for serverID=1
  MBserver.registerWorker(1, WRITE_HOLD_REGISTER, &FC06);     // FC=0x06 for serverID=1
  MBserver.registerWorker(1, WRITE_MULT_REGISTERS, &FC10);    // FC=0x16 for serverID=1

  MBserver.start(502, 1, 20000);
}

#pragma endregion MODBUS

void setup() {
  Serial.begin(115200);
  delay(1000);

  WiFi.onEvent(WiFiEvent);

  Serial.print("Init I2C ");
  if (Wire.begin(_SDA, _SCL, 400000UL)) { // 400kHz
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
    /*
    ETH.config(
      { 192, 168, 1, 110 },
      { 192, 168, 1, 1 },
      { 255, 255, 255, 0 },
      { 8, 8, 8, 8 },
      { 0, 0, 0, 0 }
    )
    */
    ETH.config(
      { 0, 0, 0, 0 },
      { 0, 0, 0, 0 },
      { 0, 0, 0, 0 },
      { 0, 0, 0, 0 },
      { 0, 0, 0, 0 }
    )
  ) {
		Serial.println("OK");
	} else {
		Serial.println("KO");
	}

  mbSetup();
  analogSetup();
}

void loop() {
  millis_t now = millis();
  pcf8574s.updateInput();
  analogUpdate(now);

  //if (pcf8574s.updateInput() >= 0) { // Was executed (could have an error if > 0)
  //  static_assert(size(pcf8574s.ins) == size(pcf8574s.outs));
  //  if (memcmp(pcf8574s.outs, pcf8574s.ins, sizeof(pcf8574s.ins)) != 0) {
  //    memcpy(pcf8574s.outs, pcf8574s.ins, sizeof(pcf8574s.ins));
  //    pcf8574s.flushOutput();
  //  }
  //}

  //if (now - lastInputPrint > 1000) {
  //  lastInputPrint = now;
  //  for (int i = 0; i < pcf8574s.inputs(); ++i) {
  //    Serial.print(pcf8574s.readInput(i) ? "H" : "L");
  //  }
  //  Serial.println();
  //}

  //if (now - lastEthSend > 10000) {
  //  lastEthSend = now;
  //  if (eth_connected) {
  //    testClient("google.com", 80);
  //  } else {
  //    Serial.println("ETH not connected");
  //  }
  //}

}
