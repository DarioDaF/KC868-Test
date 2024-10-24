//#ifdef _OUTPUT_INC_
//#error "(output.cpp) included multiple times"
//#else
//#define _OUTPUT_INC_

#ifndef _OUTPUT_H_
#define _OUTPUT_H_
#include <Stream.h>
#include "RCSwitch.h"

#define PROGRAM_VERSION       1
#define EE_ADDRESS            0
#define MIN_PACKET_DELAY_MS 200UL

struct __attribute__((packed)) s_parameters {
  int p_version;  // Program version
  int b_size;     // Bit size
  int p_len;      // Pulse length
  int protocol;   // Protocol
  int repeat;     // Repeat transmit
  bool raw;       // Print raw data

  void setdefault() {
    this->p_version = PROGRAM_VERSION;
    this->b_size    = 24;
    this->p_len     = 300;
    this->protocol  = 1;
    this->repeat    = 10;
    this->raw       = false;
  }
};

enum class e_print_mode : int16_t {
  NONE,           // Select none
  VERSION,        // Select version
  SIZE,           // Select Bit size
  PULSELEN,       // Select Pulse length
  PROTOCOL,       // Select Protocol
  REPEAT,         // Select Repeat
  RAW,            // Select Raw
  _END,

  _BEGIN = VERSION
};

static inline e_print_mode operator+(const e_print_mode lhs, const int16_t rhs) {
  return static_cast<e_print_mode>((int16_t)lhs + rhs);
}
static inline e_print_mode operator-(const e_print_mode lhs, const int16_t rhs) {
  return static_cast<e_print_mode>((int16_t)lhs - rhs);
}

void output(Print& device, bool raw_data, uint32_t code, uint16_t length,
            uint16_t delay, uint8_t protocol, unsigned int* raw);
void RCSend(RCSwitch& rc_switch, uint32_t code, uint8_t b_size,
            uint16_t p_len, uint8_t protocol, uint8_t repeat);
void RCSend(RCSwitch& rc_switch, uint32_t code, s_parameters& params);
void PrintData(Print& device, uint32_t code, int b_size, int p_len, int protocol, int repeat);
void RCPrintAndSend(Print& device, RCSwitch& rc_switch, uint32_t code,
                    uint8_t b_size, uint16_t p_len, uint8_t protocol, uint8_t repeat);
void RCPrintAndSend(Print& device, RCSwitch& rc_switch, uint32_t code, s_parameters& params);

#endif
