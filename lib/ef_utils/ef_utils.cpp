#include <ef_utils.hpp>
#include <cstdlib>

#if defined(RGB_BUILTIN_LED)
  #if __has_include("FastLED.h")
    #include <FastLED.h>
    CRGB led;
    #define CHIPSET WS2812
  #else
    #undef RGB_BUILTIN_LED
  #endif
#endif

void ledBegin(uint8_t v) {
  #if defined(RGB_BUILTIN_LED)
    FastLED.addLeds<CHIPSET, RGB_BUILTIN_LED, GRB>(&led, 1);
  #elif defined(DIGITAL_BUILTIN_LED)
    pinMode(DIGITAL_BUILTIN_LED, OUTPUT);
  #endif
  ledWrite(v);
}

void ledWrite(uint8_t v) {
  #if defined(RGB_BUILTIN_LED)
    if(v == LED_OFF)
      led.setColorCode(CRGB::Black);
    else
      led.setColorCode(CRGB::White);
    FastLED.setBrightness(LED_BRIGHTNESS);
    FastLED.show();
  #elif defined(DIGITAL_BUILTIN_LED)
    digitalWrite(DIGITAL_BUILTIN_LED, v);
  #endif
}

String strfmt(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  int char_needed = vsnprintf(nullptr, 0, fmt, args) + 1;
  char* result = (char*)malloc(char_needed);
  vsnprintf(result, char_needed, fmt, args);
  va_end(args);
  return result;
}

bool txtToUl(const char *n, uint32_t &v, const int base) {
  char *_end_ptr;
  v = strtoul(n, &_end_ptr, base);
  return _end_ptr[0] == '\0';
}

char readChar(Stream& device, e_char_type char_type, bool echo) {
  char ch = '\0';
  if(device.available()) {
    ch = device.read();
    if(echo) device.write(ch);
    switch(char_type) {
      case e_char_type::lower:
        ch = (char)tolower(ch);
        break;
      case e_char_type::upper:
        ch = (char)toupper(ch);
        break;
    }
  }
  return ch;
}

bool readRow(Stream& device, String &s, e_char_type char_type, bool echo) {
  while(device.available()) {
    char ch = device.read();
    if(echo) device.write(ch);
    if(ch == '\n') {
      return true;
    } else if(ch == '\b') {
      s.remove(s.length() - 1);
    } else if(ch != '\r') {
      switch(char_type) {
        case e_char_type::lower:
          s += (char)tolower(ch);
          break;
        case e_char_type::upper:
          s += (char)toupper(ch);
          break;
        default:
          s += ch;
          break;
      }
    }
  }
  return false;
}

String readRow(Stream& device, e_char_type char_type, bool echo) {
  String s = "";
  while(!readRow(device, s, char_type, echo))
    yield();
  return s;
}

std::map<String, String> explodeParams(const String &s, const String delimiters, const char separator) {
  std::map<String, String> res;
  StrPart parts { s };
  while (parts.nextAny(delimiters)) {
    String part = parts.get();
    StrPart kv { part };
    kv.next(separator);
    res[kv.get()] = kv.rest();
  }
  return res;
}
