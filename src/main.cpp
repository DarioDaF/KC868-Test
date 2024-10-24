#include <Arduino.h>
#include <Wire.h>
#include <PCF8574_KC868.hpp>
#include <SPI.h>
#include <ETH.h>
#include <EEPROM.h>
#include <polyfill.hpp>

#define serialProg Serial
#define sLog Serial
#define MBserial Serial1

#include <ef_utils.hpp>

#pragma region PIN DEFINITIONS

#define SDA_I2C GPIO_NUM_4
#define SCL_I2C GPIO_NUM_5
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

#pragma region GLOBAL DECLARATIONS

#define EE_MAGIC 0xEF01

struct __attribute__((packed)) s_settings {
  size_t length = sizeof(*this);
  uint16_t magic = EE_MAGIC;
  uint8_t ip[4] = { 0 , 0, 0, 0 };
  uint8_t subnet[4] = { 0 , 0, 0, 0 };
  uint8_t gateway[4] = { 0 , 0, 0, 0 };
  uint8_t dns1[4] = { 0 , 0, 0, 0 };
  uint8_t dns2[4] = { 0 , 0, 0, 0 };
  uint16_t mb_id = 1;
  uint16_t mb_port = 502;
};

millis_t lastInputPrint = 0;
millis_t lastEthSend = 0;
PCF8574_KC868<2, 2> pcf8574s({ 0x22, 0x21 }, { 0x24, 0x25 }, 50);
s_settings settings;

void loadSettings(const int address, s_settings &p);
void saveSettings(const int address, s_settings &p);
void eeInit(int _size);
void printSettings(Print& device, s_settings& s, bool showPass = false);
void readSettings(Stream& device);
void execCommand(Stream& device);
void reboot();
void strtoip(uint8_t* addr, String s);
void WiFiEvent(WiFiEvent_t event);
void setupAnalog();
void updateAnalog(millis_t now);

#pragma endregion GLOBAL DECLARATIONS

#pragma region RC433MHz

#include <RCSwitch.h>
#include <output.h>

struct s_packet {
  uint32_t value;
  unsigned int protocol; // @TODO: Probably better bit and delay

  void invalidate() {
    this->value = 0;
    this->protocol = (unsigned int)-1;
  }
  bool isValid() {
    return this->protocol !=  (unsigned int)-1;
  }
  bool operator == (const s_packet o) {
    return this->value == o.value && this->protocol == o.protocol;
  }
  bool operator != (const s_packet o) {
    return !(*this == o);
  }
};

struct __attribute__((packed)) s_code {
  uint32_t code;    // Code value
  uint8_t b_size;   // Bit size
  int16_t p_len;    // Pulse length
  uint8_t protocol; // Protocol
  uint8_t repeat;   // Repeat transmit
};

const s_code a_send[] PROGMEM = {
  {16533736UL, 24, 300, 1, 5},
  {16533732UL, 24, 300, 1, 5},
  {16533730UL, 24, 300, 1, 5},
  {16533729UL, 24, 300, 1, 5},

  {16533864UL, 24, 300, 1, 5},
  {16533860UL, 24, 300, 1, 5},
  {16533858UL, 24, 300, 1, 5},
  {16533857UL, 24, 300, 1, 5},

  {16533992UL, 24, 300, 1, 5},
  {16533988UL, 24, 300, 1, 5},
  {16533986UL, 24, 300, 1, 5},
  {16533985UL, 24, 300, 1, 5},

  {10242160UL, 24, 305, 1, 5},
  {10242156UL, 24, 310, 1, 5},
  {10242154UL, 24, 315, 1, 5},
  {10242153UL, 24, 320, 1, 5},

  {10242152UL, 24, 300, 1, 5},  // Portone garage
  {10242148UL, 24, 300, 1, 5},  // Luce estena
  {4647576UL, 24, 470, 1, 5},   // Citofono
  {4542712UL, 24, 470, 1, 5}    // Campanello esterno
};

RCSwitch ioSwitch = RCSwitch();
s_packet last_packet = { .value = 0, .protocol = (uint16_t)-1 };
millis_t last_packet_time = 0UL;

void setupRC433() {
  pinMode(RX_433M, INPUT);
  pinMode(TX_433M, OUTPUT);
  ioSwitch.enableReceive(digitalPinToInterrupt(RX_433M));
  ioSwitch.enableTransmit(TX_433M);
}

void sendRC433(Print& device, size_t idx) {
  if(idx < eflib::size(a_send)) {
    s_code send;
    memcpy_P(&send, (PGM_P)&a_send[idx], sizeof(send));
    RCPrintAndSend(device, ioSwitch, send.code, send.b_size, send.p_len, send.protocol, send.repeat);
  }
}

void updateRC433(Print& device, millis_t now) {
  if (last_packet.isValid() && (now - last_packet_time >= packet_delay_ms)) {
    device.println(F("Button released"));
    last_packet.invalidate();
  }
  if (ioSwitch.available()) {
    millis_t now = millis();
    s_packet currentPacket = { .value = ioSwitch.getReceivedValue(), .protocol = ioSwitch.getReceivedProtocol() };
    if (currentPacket.isValid() && (currentPacket != last_packet)) {
      device.println(F("Button pressed"));
      printRC(device, false, currentPacket.value, ioSwitch.getReceivedBitlength(),
              ioSwitch.getReceivedDelay(), currentPacket.protocol, ioSwitch.getReceivedRawdata());
    }
    last_packet = currentPacket;
    last_packet_time = now;
    ioSwitch.resetAvailable();
  }
}

#pragma endregion RC433MHz

#pragma region EEPROM

#define EE_SIZE 1024

void loadSettings(const int address, s_settings &p) {
  if((address + sizeof(p)) < EEPROM.length()) {
    EEPROM.get(address, p);
    if((p.magic == EE_MAGIC) && (p.length == sizeof(s_settings))) {
      logoutln(F("Settings were loaded from the eeprom."));
    } else {
      s_settings default_p;
      p = default_p;
      logoutln(F("Invalid version loading default settings."));
    }
  } else logoutln(F("Eeprom size too small."));
}

void saveSettings(const int address, s_settings& p) {
  if((address + sizeof(p)) < EEPROM.length()) {
    EEPROM.put(address, p);
    EEPROM.commit();
    logoutln(F("Settings were stored in the eeprom."));
  } else logoutln(F("Eeprom size too small."));
}

void eeInit(int _size) {
  if(EEPROM.begin(_size)) {
    logoutln(F("Eeprom successfully initialized."));
  } else {
    logoutln(F("Eeprom initialization error."));
  }
}

#pragma endregion EEPROM

#pragma region COMMAND

void printSettings(Print& device, s_settings& s, bool showPass) {
  device.println(F("[Registered Settings]"));

  if(lib::isSet(s.ip)) {
    device.print(F("Local IP Address: "));
    device.println(IPAddress(s.ip));
    device.print(F("Subnet Mask: "));
    device.println(IPAddress(s.subnet));
    device.print(F("Gateway IP Address: "));
    device.println(IPAddress(s.gateway));
    device.print(F("Dns1 IP Address: "));
    device.println(IPAddress(s.dns1));
    device.print(F("Dns2 IP Address: "));
    device.println(IPAddress(s.dns2));
  } else {
    device.println(F("Local IP Address: Auto"));
  }

  device.print(F("Modbus Id: "));
  device.println(s.mb_id);

  device.print(F("Modbus Port: "));
  device.println(s.mb_port);
}

void readSettings(Stream& device) {
  s_settings news;

  device.println(F("[New Settings]"));

  device.print(F("Local IP Address: "));
  strtoip(news.ip, readRow(device, e_char_type::normal, true));
  if(lib::isSet(IPAddress(news.ip))) {
    device.print(F("Subnet Mask: "));
    strtoip(news.subnet, readRow(device, e_char_type::normal, true));

    device.print(F("Gateway IP Address: "));
    strtoip(news.gateway, readRow(device, e_char_type::normal, true));

    device.print(F("Dns1 IP Address: "));
    strtoip(news.dns1, readRow(device, e_char_type::normal, true));

    device.print(F("Dns2 IP Address: "));
    strtoip(news.dns2, readRow(device, e_char_type::normal, true));
  }

  device.print(F("Modbus Id Number: "));
  news.mb_id = readRow(device, e_char_type::normal, true).toInt();

  device.print(F("Modbus Port: "));
  news.mb_port = readRow(device, e_char_type::normal, true).toInt();

  device.println();
  printSettings(device, news, true);
  device.print(F("Save? [N/y]: "));
  if(readRow(device, e_char_type::lower, true) == "y") {
    settings = news;
    saveSettings(0, settings);
    reboot();
  }
}

void execRC433(Stream& device) {
  device.println(F("[Send registered data to RC433]"));
  device.print(F("Select row [0..19]: "));
  String s = readRow(device, e_char_type::upper, true);
  sendRC433(device, s.toInt());
}

void execCommand(Stream& device) {
  char query = readChar(device, e_char_type::upper);
  switch(query) {
    case '\0':
      break;
    case 'N':
      execRC433(device);
      break;
    case 'P':
      printSettings(device, settings);
      break;
    case 'S':
      readSettings(device);
      break;
    case 'R':
      reboot(); // noreturn
      break;
    default:
      break;
  }
}

__attribute__((noreturn)) void reboot() {
  logoutln(F("Wait for Reboot..."));
  ESP.restart();
  while(true);
}

#pragma endregion COMMAND

#pragma region ETHERNET

static bool eth_connected = false;

void strtoip(uint8_t* addr, String s) {
  IPAddress ip_obj;
  ip_obj.fromString(s);
  const auto v4 = lib::v4(ip_obj);
  memcpy(addr, &v4, 4);
}

void WiFiEvent(WiFiEvent_t event) {
  switch (event) {
    case ARDUINO_EVENT_ETH_START:
      logoutln(F("ETH Started"));
      //set eth hostname here
      ETH.setHostname("esp32-ethernet");
      break;
    case ARDUINO_EVENT_ETH_CONNECTED:
      logoutln(F("ETH Connected"));
      break;
    case ARDUINO_EVENT_ETH_GOT_IP:
      logout(F("ETH MAC: "));
      logout(ETH.macAddress());
      logout(F(", IPv4: "));
      logout(ETH.localIP());
      if (ETH.fullDuplex()) {
        logout(F(", FULL_DUPLEX"));
      }
      logout(F(", "));
      logout(ETH.linkSpeed());
      logoutln(F("Mbps"));
      eth_connected = true;
      break;
    case ARDUINO_EVENT_ETH_DISCONNECTED:
      logoutln(F("ETH Disconnected"));
      eth_connected = false;
      break;
    case ARDUINO_EVENT_ETH_STOP:
      logoutln(F("ETH Stopped"));
      eth_connected = false;
      break;
    default:
      break;
  }
}

void setupETH(Print& device) {
  WiFi.onEvent(WiFiEvent);

  if(ETH.begin(0, -1, ETH_PHY_MDC/*23*/, ETH_PHY_MDIO/*18*/, eth_phy_type_t::ETH_PHY_LAN8720, eth_clock_mode_t::ETH_CLOCK_GPIO17_OUT/*17*/, true)) {
		device.println(F("Init ETH OK"));
	} else {
		device.println(F("Init ETH KO"));
	}

  if(ETH.config(settings.ip, settings.gateway, settings.subnet, settings.dns1, settings.dns2)) {
		device.println(F("Config ETH OK"));
	} else {
		device.println(F("Config ETH KO"));
	}
}

void testClient(const char* host, uint16_t port) {
  logout(F("\nconnecting to "));
  logoutln(host);

  WiFiClient client;
  if (!client.connect(host, port)) {
    logoutln(F("Connection failed"));
    return;
  }
  client.printf("GET / HTTP/1.1\r\nHost: %s\r\n\r\n", host);
  while (client.connected() && !client.available());
  while (client.available()) {
    logoutwr(client.read());
  }

  logoutln(F("closing connection\n"));
  client.stop();
}

#pragma endregion ETHERNET

#pragma region ANALOGS

uint16_t analog[eflib::size(pinAnalog)];
constexpr millis_t updateAnalogInterval = 100;
millis_t lastAnalogUpdate = 0;

void setupAnalog() {
  analogSetAttenuation(ADC_11db);
  analogReadResolution(12);
}

void updateAnalog(millis_t now) {
  if(now - lastAnalogUpdate >= updateAnalogInterval) {
    for(size_t i = 0; i < eflib::size(analog); ++i)
      analog[i] = analogRead(pinAnalog[i]);
    lastAnalogUpdate = now;
  }
}

#pragma endregion ANALOGS

#pragma region MODBUS

#include <ModbusServerTCPasync.h>
#include <ModbusServerRTU.h>

// Create server
ModbusServerTCPasync MBTcpServer;
ModbusServerRTU MBserver(2000);

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

  if((start + numWords) > eflib::size(hold_registers)) {
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

  if ((start + numWords) > eflib::size(analog)) {
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

  if(addr >= eflib::size(hold_registers)) {
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

  if ((start + numWords) > eflib::size(hold_registers)) {
    response.setError(request.getServerID(), request.getFunctionCode(), ILLEGAL_DATA_ADDRESS);
  } else {
    for(size_t i = 0; i < numWords; ++i)
      offset = request.get(offset, hold_registers[i + start]);
    response.add(request.getServerID(), request.getFunctionCode(), start, numWords);
  }
  return response;
}

void setupModbus() {
  if(settings.mb_port) {
    // Define and start RTU server
    MBTcpServer.registerWorker(settings.mb_id, READ_COIL, &FC01);             // FC=0x01 for serverID=1
    MBTcpServer.registerWorker(settings.mb_id, READ_DISCR_INPUT, &FC02);      // FC=0x02 for serverID=1
    MBTcpServer.registerWorker(settings.mb_id, READ_HOLD_REGISTER, &FC03);    // FC=0x03 for serverID=1
    MBTcpServer.registerWorker(settings.mb_id, READ_INPUT_REGISTER, &FC04);   // FC=0x04 for serverID=1
    MBTcpServer.registerWorker(settings.mb_id, WRITE_COIL, &FC05);            // FC=0x05 for serverID=1
    MBTcpServer.registerWorker(settings.mb_id, WRITE_MULT_COILS, &FC0F);      // FC=0x0F for serverID=1
    MBTcpServer.registerWorker(settings.mb_id, WRITE_HOLD_REGISTER, &FC06);   // FC=0x06 for serverID=1
    MBTcpServer.registerWorker(settings.mb_id, WRITE_MULT_REGISTERS, &FC10);  // FC=0x16 for serverID=1
    MBTcpServer.start(settings.mb_port, settings.mb_id, 20000);
  }
  {
    MBserver.registerWorker(settings.mb_id, READ_COIL, &FC01);                // FC=0x01 for serverID=1
    MBserver.registerWorker(settings.mb_id, READ_DISCR_INPUT, &FC02);         // FC=0x02 for serverID=1
    MBserver.registerWorker(settings.mb_id, READ_HOLD_REGISTER, &FC03);       // FC=0x03 for serverID=1
    MBserver.registerWorker(settings.mb_id, READ_INPUT_REGISTER, &FC04);      // FC=0x04 for serverID=1
    MBserver.registerWorker(settings.mb_id, WRITE_COIL, &FC05);               // FC=0x05 for serverID=1
    MBserver.registerWorker(settings.mb_id, WRITE_MULT_COILS, &FC0F);         // FC=0x0F for serverID=1
    MBserver.registerWorker(settings.mb_id, WRITE_HOLD_REGISTER, &FC06);      // FC=0x06 for serverID=1
    MBserver.registerWorker(settings.mb_id, WRITE_MULT_REGISTERS, &FC10);     // FC=0x16 for serverID=1
    MBserver.begin(MBserial);
  }
}

#pragma endregion MODBUS

void setup() {
  serialProg.begin(115200);
  delay(1000);

  if(MBserial != serialProg) {
    MBserial.setPins(RX_RS485, TX_RS485, GPIO_NUM_NC, GPIO_NUM_NC);
    MBserial.begin(115200, SERIAL_8N1);
    MBserial.println(F("MBserial port is initializzed"));
  } else {
    serialProg.println(F("MBserial port not initializzed"));
  }

  serialProg.print("Init I2C ");
  if (Wire.begin(SDA_I2C, SCL_I2C, 400000UL)) { // 400kHz
		serialProg.println("OK");
	} else {
		serialProg.println("KO");
	}

  eeInit(EE_SIZE);
  loadSettings(0, settings);

  serialProg.print("Init PCF8574 ");
	if (pcf8574s.begin()) {
		serialProg.println("OK");
	} else {
		serialProg.println("KO");
	}

  setupETH(serialProg);

  setupModbus();
  setupAnalog();
  setupRC433();
}

void loop() {
  execCommand(serialProg);
  yield();
  millis_t now = millis();
  updateRC433(serialProg, now);
  pcf8574s.updateInput();
  updateAnalog(now);

  //if (pcf8574s.updateInput() >= 0) { // Was executed (could have an error if > 0)
  //  static_assert(eflib::size(pcf8574s.ins) == eflib::size(pcf8574s.outs));
  //  if (memcmp(pcf8574s.outs, pcf8574s.ins, sizeof(pcf8574s.ins)) != 0) {
  //    memcpy(pcf8574s.outs, pcf8574s.ins, sizeof(pcf8574s.ins));
  //    pcf8574s.flushOutput();
  //  }
  //}

  //if (now - lastInputPrint > 1000) {
  //  lastInputPrint = now;
  //  for (int i = 0; i < pcf8574s.inputs(); ++i) {
  //    logout(pcf8574s.readInput(i) ? "H" : "L");
  //  }
  //  logoutln();
  //}

  //if (now - lastEthSend > 10000) {
  //  lastEthSend = now;
  //  if (eth_connected) {
  //    testClient("google.com", 80);
  //  } else {
  //    logoutln("ETH not connected");
  //  }
  //}

}
