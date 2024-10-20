
#include <Arduino.h>
#include <Wire.h>

// Requres to init Wire first with intended pins and speed

template<int N_IN, int N_OUT>
class PCF8574_KC868 {
  public:
    PCF8574_KC868(const uint8_t (&addr_in)[N_IN], const uint8_t (&addr_out)[N_OUT], unsigned long updateInpuInterval = 50, TwoWire& wire = Wire)
    : _wire(wire), updateInpuInterval(updateInpuInterval) {
      memcpy(this->addr_in, addr_in, sizeof(this->addr_in));
      memcpy(this->addr_out, addr_out, sizeof(this->addr_out));
      memset(this->ins, 0xFF, sizeof(this->ins));
      memset(this->outs, 0xFF, sizeof(this->outs));
    }
    constexpr size_t outputs() {
      return N_OUT * 8;
    }
    constexpr size_t inputs() {
      return N_IN * 8;
    }
    bool begin(bool doInitialRead = true) {
      int failures = 0;

      // Set in input mode the inputs
      for (uint8_t block = 0; block < N_IN; ++block) {
        _wire.beginTransmission(addr_in[block]);
        _wire.write(0xFF);
        if (_wire.endTransmission() != 0) {
          failures += 1;
        }
      }

      failures += flushOutput();
      if (doInitialRead) {
        failures += flushInput();
      }
      return failures == 0;
    }
    int updateInput() { // Returns number of write failures, -1 means no action
      if ((millis() - lastInputUpdate) >= updateInpuInterval) {
        return flushInput();
      }
      return -1;
    }
    int flushOutput() { // Returns number of write failures
      int failures = 0;

      for (uint8_t block = 0; block < N_OUT; ++block) {
        _wire.beginTransmission(addr_out[block]);
        _wire.write(outs[block]);
        if (_wire.endTransmission() != 0) {
          failures += 1;
        }
      }

      return failures;
    }
    int flushInput() { // Returns number of write failures
      lastInputUpdate = millis();
      int failures = 0;

      for (uint8_t block = 0; block < N_IN; ++block) {
        if (_wire.requestFrom(addr_in[block], (uint8_t)1) == (uint8_t)1) {
          ins[block] = _wire.read();
        } else {
          failures += 1;
        }
      }

      return failures;
    }
    bool readInput(int n) {
      if ((n < 0) || (n >= N_IN * 8)) {
        return true;
      }
      return ins[n / 8] & _BV(n % 8);
    }
    bool readOutput(int n) {
      if ((n < 0) || (n >= N_OUT * 8)) {
        return true;
      }
      return outs[n / 8] & _BV(n % 8);
    }
    void writeOutput(int n, bool val) {
      if ((n < 0) || (n >= N_OUT * 8)) {
        return;
      }
      if (val) {
        outs[n / 8] |= _BV(n % 8);
      } else {
        outs[n / 8] &= ~_BV(n % 8);
      }
    }
    unsigned long updateInpuInterval;
    uint8_t ins[N_IN];
    uint8_t outs[N_OUT];
  private:
    TwoWire& _wire;
    uint8_t addr_in[N_IN];
    uint8_t addr_out[N_OUT];
    unsigned long lastInputUpdate;
};
