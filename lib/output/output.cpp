
#include <Arduino.h>
#include <EEPROM.h>
#include <ef_utils.hpp>
#include "output.h"

void printRC(Print& device, bool raw_data, uint32_t code, uint16_t length,
            uint16_t delay, uint8_t protocol, unsigned int* raw) {
  String _str = eflib::toBinary(code, length).c_str();
  device.print(F("Data received <- "));
  device.print(F("Dec="));
  device.print(code);
  device.print(F(", Hex="));
  device.print(strfmt("%08lX", code));
  device.print(F(", Bin="));
  device.println(_str);
  device.print(F("Packet info:     "));
  device.print(F("Bit-size="));
  device.print(length);
  device.print(F(", Pulse-lenght="));
  device.print(delay);
  device.print(F(", Protocol="));
  device.println(protocol);
  if (raw_data) {
    device.print(F("Raw data: "));
    for (uint16_t i = 0; i <= length * 2; i++) {
      if (i != 0) device.print(F(","));
      device.print(raw[i]);
    }
    device.println();
  }
}

void RCSend(RCSwitch& rc_switch, uint32_t code, uint8_t b_size,
            uint16_t p_len, uint8_t protocol, uint8_t repeat) {
  digitalWrite(LED_BUILTIN, HIGH);
  rc_switch.setRepeatTransmit(repeat);
  rc_switch.setProtocol(protocol, p_len);
  rc_switch.send(code, b_size);
  digitalWrite(LED_BUILTIN, LOW);
}

void RCSend(RCSwitch& rc_switch, uint32_t code, s_parameters& params) {
  digitalWrite(LED_BUILTIN, HIGH);
  rc_switch.setRepeatTransmit(params.repeat);
  rc_switch.setProtocol(params.protocol, params.p_len);
  rc_switch.send(code, params.b_size);
  digitalWrite(LED_BUILTIN, LOW);
}

void PrintData(Print& device, uint32_t code, int b_size, int p_len, int protocol, int repeat) {
  device.print(F("Data transmit -> Dec="));
  device.print(strfmt("%lu", code));
  device.print(F(", Hex="));
  device.print(strfmt("%08LX", code));
  device.print(F(", Bin="));
  device.println(eflib::toBinary(code, b_size).c_str());
  device.print(F("Packet info:     Bit-size="));
  device.print(b_size);
  device.print(F(", Pulse-lenght="));
  device.print(p_len);
  device.print(F(", Protocol="));
  device.print(protocol);
  device.print(F(", Repeat="));
  device.println(repeat);
}

void RCPrintAndSend(Print& device, RCSwitch& rc_switch, uint32_t code,
                    uint8_t b_size, uint16_t p_len, uint8_t protocol, uint8_t repeat) {
  PrintData(device, code, b_size, p_len, protocol, repeat);
  RCSend(rc_switch, code, b_size, p_len, protocol, repeat);
  device.println(F("Data transmission completed"));
}

void RCPrintAndSend(Print& device, RCSwitch& rc_switch, uint32_t code, s_parameters& params) {
  PrintData(device, code, params.b_size, params.p_len, params.protocol, params.repeat);
  RCSend(rc_switch, code, params);
  device.println(F("Data transmission completed"));
}
