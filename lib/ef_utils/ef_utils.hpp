#if !defined(_EF_UTILS_HPP_)
#define _EF_UTILS_HPP_

#include <Arduino.h>
#include <map>

//#define sLog (&Serial)
#if defined(sLog)
  #define logoutwr(msg)     { sLog.write(msg); sLog.flush(); }
  #define logout(msg)       { sLog.print(msg); sLog.flush(); }
  #define logoutln(msg)     { sLog.println(msg); sLog.flush(); }
  #define logoutf(fmt, ...) { sLog.printf(fmt, __VA_ARGS__); sLog.flush(); }
#else
  #define logoutwr(msg)
  #define logout(msg)
  #define logoutln(msg)
  #define logoutf(fmt, ...)
#endif

#define LED_ON LOW
#define LED_OFF HIGH
#define LED_BRIGHTNESS 64

enum class e_char_type {
  normal, lower, upper,
  _MIN=normal, _MAX=upper+1
};

//struct s_RGB {
//  union {
//    uint32_t rgb;
//    uint8_t argb[4];
//    struct __attribute__((packed)) {
//      union {
//        uint8_t b;     // Blue channel value
//        uint8_t blue;  // Blue channel value
//      };
//      union {
//        uint8_t g;     // Green channel value
//        uint8_t green; // Green channel value
//      };
//      union {
//        uint8_t r;     // Red channel value
//        uint8_t red;   // Red channel value
//      };
//      union {
//        uint8_t a;     // Alpha channel not used
//        uint8_t alpha; // Alpha channel not used
//      };
//    };
//  };
//};

struct s_RGB {
  union {
    uint32_t rgb;
    uint8_t argb[4];
    struct __attribute__((packed)) {
      uint8_t b; // Blue channel value
      uint8_t g; // Green channel value
      uint8_t r; // Red channel value
      uint8_t a; // Alpha channel not used
    };
  };
};

//typedef s_RGB uint32_rgb;
//typedef unsigned long millis_t;
//typedef unsigned long micros_t;

using uint32_rgb = s_RGB;
using millis_t = unsigned long;
using micros_t = unsigned long;

char readChar(Stream& device, e_char_type char_type = e_char_type::normal, bool echo = false);
bool readRow(Stream& device, String &s, e_char_type char_type = e_char_type::normal, bool echo = false);
String readRow(Stream& device, e_char_type char_type = e_char_type::normal, bool echo = false);
String strfmt(const char* fmt, ...);
bool txtToUl(const char *n, uint32_t &v, const int base = 10);
std::map<String, String> explodeParams(const String &s, const String delimiters = " ,;", const char separator = '=');
void ledBegin(uint8_t v = LED_OFF);
void ledWrite(uint8_t v);

namespace eflib {
  template<typename T>
  T scale(const T value, const T iLow, const T iHigh, const T oLow, const T oHigh) {
    return (value-iLow)*(oHigh-oLow)/(iHigh-iLow)+oLow;
  }

  template<typename T>
  T valuetoenum(int v, const T def = T::_MAX) {
    if ((v >= (int)T::_MIN) && (v <= (int)T::_MAX))
      return static_cast<T>(v);
    return def;
  }

  template<typename T>
  int enumtovalue(const T e) { return (int)e; }

  #if defined(ESP32)
    template <class T, size_t N>
    constexpr size_t size(const T (&array)[N]) noexcept { return N; }
  #else
    using std::size;
  #endif

  template<typename T>
  bool bit_Read(T& value, const uint8_t bit) {
    uint8_t byte = bit / 8;
    if (byte >= sizeof(T)) return false;
    uint8_t* p = (uint8_t*)&value;
    return (p[byte] >> (bit % 8)) & 1;
  }

  template<typename T>
  bool bit_Set(T& value, const uint8_t bit) {
    uint8_t byte = bit / 8;
    if (byte >= sizeof(T)) return false;
    uint8_t* p = (uint8_t*)&value;
    p[byte] |= 1 << (bit % 8);
    return true;
  }

  template<typename T>
  bool bit_Clear(T& value, const uint8_t bit) {
    uint8_t byte = bit / 8;
    if (byte >= sizeof(T)) return false;
    uint8_t* p = (uint8_t*)&value;
    p[byte] &= ~(1 << (bit % 8));
    return true;
  }

  template<typename T>
  bool bit_Togle(T& value, const uint8_t bit) {
    uint8_t byte = bit / 8;
    if (byte >= sizeof(T)) return false;
    uint8_t* p = (uint8_t*)&value;
    p[byte] ^= (1 << (bit % 8));
    return true;
  }

  template<typename T>
  bool bit_Write(T& value, const uint8_t bit, bool bitvalue) {
    return bitvalue ? bit_Set(value, bit) : bit_Clear(value, bit);
  }

  template <typename T>
  String toBinary(T value, size_t len = 0) {
    String str = "";
    size_t lenType = sizeof(value) * 8;
    for (size_t i = (len != 0) ? len : lenType; i > 0; i--)
      str += (i > lenType) ? '0' : bit_Read(value, i - 1) ? '1' : '0';
    return str;
  }

  template <typename T>
  T fillBits(size_t n, bool value = true) {
    T x = 0;
    if ((n > 0) && (n <= sizeof(x) * 8))
      for (size_t i = 0; i < n; i++)
        bit_Write(x, i, value);
    return x;
  }

  template <typename T>
  T toggleBits(size_t n) {
    T x = 0;
    if ((n > 0) && (n <= sizeof(x) * 8))
      for (size_t i = 0; i < n; i++)
        bit_Toggle(x, i);
    return x;
  }

  template <typename T>
  String fillString(T str, const size_t num, const char paddingChar = ' ') {
    String _str = str;
    while (_str.length() < num) { _str += paddingChar; }
    return _str;
  }
}


struct StrPart {
  const String &s;
  int p0;
  int p1;

  StrPart(const String &s) : s(s), p0(0), p1(-1) {}

  bool next(char separator) {
    if ((p1 >= 0) && (p1 >= s.length()))
      return false;
    p0 = p1 + 1;
    p1 = s.indexOf(separator, p0);
    if (p1 < 0)
      p1 = s.length();
    return true;
  }

  bool next(String separator) {
    if ((p1 >= 0) && (p1 >= s.length()))
      return false;
    p0 = p1 + 1;
    p1 = s.indexOf(separator, p0);
    if (p1 < 0)
      p1 = s.length();
    return true;
  }

  bool nextAny(String delimiters) {
    if ((p1 >= 0) && (p1 >= s.length()))
      return false;
    p0 = p1 + 1;
    p1 = indexOfAny(s, delimiters, p0);
    if (p1 < 0)
      p1 = s.length();
    return true;
  }

  int indexOfAny(const String &s, const String delimiters, int start) {
    for (int i = start; i < s.length(); ++i) {
      if (delimiters.indexOf(s[i]) >= 0) {
        return i;
      }
    }
    return -1;
  }

  String get() {
    return s.substring(p0, p1);
  }
  String rest() {
    return s.substring(p1 + 1);
  }
};

#endif
