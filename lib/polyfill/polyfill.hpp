#if !defined(_POLYFILL_HPP_)
#define _POLYFILL_HPP_

#ifdef ESP32
  #include <WiFi.h>
#else
  #include <ESP8266WiFi.h>
#endif


// Can't contexpr because IPAddress doesn't have a constexpr constructor
#define IP_ZERO { 0, 0, 0, 0 }

namespace lib {
  // General use
  #ifdef ESP32
    template <typename _Tp, size_t _Nm>
    constexpr inline size_t size(const _Tp (&)[_Nm]) noexcept { return _Nm; }
  #else
    using std::size;
  #endif
  class PrintBuff : public Print {
    private:
      char* buff;
      size_t pos;
      size_t buffSize;
      bool myBuff;
    public:
      PrintBuff(size_t buffSize, char* buff = nullptr) : buff(buff), pos(0), buffSize(buffSize), myBuff(false) {
        if (this->buff == nullptr) {
          this->myBuff = true;
          this->buff = new char[this->buffSize];
        }
      }
      virtual ~PrintBuff() {
        if (this->myBuff) {
          delete[] this->buff;
          this->buff = nullptr;
          this->buffSize = 0;
          this->myBuff = false;
        }
      }
      size_t getPos() const { return this->pos; }
      const char* getBuff() const { return this->buff; }
      virtual size_t write(uint8_t x) override {
        //return this->write(&x, 1);
        if (this->pos < this->buffSize) {
          this->buff[this->pos++] = x;
          return 1;
        }
        return 0;
      }
      virtual size_t write(const uint8_t *buffer, size_t size) override {
        size_t toCopy = min(size, this->buffSize - this->pos);
        memcpy(&this->buff[this->pos], buffer, toCopy);
        this->pos += toCopy;
        return toCopy;
      }
  };
  class PrintBuffStr : public Print {
    private:
      String buff;
    public:
      PrintBuffStr() : buff("") {}
      virtual ~PrintBuffStr() {}
      size_t getPos() const { return this->buff.length(); }
      const char* getBuff() const { return this->buff.c_str(); }
      virtual size_t write(uint8_t x) override {
        this->buff += (char)x;
        return 1;
      }
      virtual size_t write(const uint8_t *buffer, size_t size) override {
        this->buff.concat((char*)buffer, size);
        return size;
      }
  };

  // IPAddress
  inline const uint32_t v4(const IPAddress& ip) {
    #ifdef ESP32
      return (uint32_t)ip;
    #else
      return ip.v4();
    #endif
  }

  inline bool isSet(const IPAddress& ip) {
    #ifdef ESP32
      return ip != IPAddress IP_ZERO;
    #else
      return ip.isSet();
    #endif
  }

  // WiFi
  #ifdef ESP32

    using WiFiDisconnectReason = wifi_err_reason_t;

    struct WiFiEventStationModeConnected {
      String ssid;
      uint8_t bssid[6];
      uint8_t channel;
    };
    struct WiFiEventStationModeGotIP {
      IPAddress ip;
      IPAddress mask;
      IPAddress gw;
    };
    struct WiFiEventStationModeDisconnected {
      String ssid;
      uint8_t bssid[6];
      WiFiDisconnectReason reason;
    };
    class WiFiEventHandlerOpaque {
      private:
        WiFiClass& _WiFi;
        wifi_event_id_t _id;
      public:
        WiFiEventHandlerOpaque(WiFiClass& _WiFi, wifi_event_id_t _id) : _WiFi(_WiFi), _id(_id) {}
        virtual ~WiFiEventHandlerOpaque() {
          _WiFi.removeEvent(_id);
        }
    };
    using WiFiEventHandler = std::shared_ptr<WiFiEventHandlerOpaque>;
    inline WiFiEventHandler onStationModeConnected(WiFiClass& _WiFi, std::function<void(const WiFiEventStationModeConnected&)> f) {
      auto id = _WiFi.onEvent([f] (arduino_event_id_t event, arduino_event_info_t _info) {
        auto& info = _info.wifi_sta_connected;
        WiFiEventStationModeConnected res {
          .ssid = String(info.ssid, info.ssid_len),
          .channel = info.channel
        };
        memcpy(res.bssid, info.bssid, sizeof(res.bssid));
        f(res);
      }, ARDUINO_EVENT_WIFI_STA_CONNECTED);
      return std::make_shared<WiFiEventHandlerOpaque>(_WiFi, id);
    }
    inline WiFiEventHandler onStationModeGotIP(WiFiClass& _WiFi, std::function<void(const WiFiEventStationModeGotIP&)> f) {
      auto id = _WiFi.onEvent([f] (arduino_event_id_t event, arduino_event_info_t _info) {
        auto& info = _info.got_ip;
        WiFiEventStationModeGotIP res {
          .ip = IPAddress(info.ip_info.ip.addr),
          .mask = IPAddress(info.ip_info.netmask.addr),
          .gw = IPAddress(info.ip_info.gw.addr)
        };
        f(res);
      }, ARDUINO_EVENT_WIFI_STA_GOT_IP);
      return std::make_shared<WiFiEventHandlerOpaque>(_WiFi, id);
    }
    inline WiFiEventHandler onStationModeDisconnected(WiFiClass& _WiFi, std::function<void(const WiFiEventStationModeDisconnected&)> f) {
      auto id = _WiFi.onEvent([f] (arduino_event_id_t event, arduino_event_info_t _info) {
        auto& info = _info.wifi_sta_disconnected;
        WiFiEventStationModeDisconnected res {
          .ssid = String(info.ssid, info.ssid_len),
          .reason = (WiFiDisconnectReason)info.reason
        };
        memcpy(res.bssid, info.bssid, sizeof(res.bssid));
        f(res);
      }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
      return std::make_shared<WiFiEventHandlerOpaque>(_WiFi, id);
    }
  #else
    using ::WiFiDisconnectReason;
    using ::WiFiEventStationModeConnected;
    using ::WiFiEventStationModeGotIP;
    using ::WiFiEventStationModeDisconnected;
    using ::WiFiEventHandler;
    inline WiFiEventHandler onStationModeConnected(ESP8266WiFiClass& _WiFi, std::function<void(const WiFiEventStationModeConnected&)> f) {
      return _WiFi.onStationModeConnected(f);
    }
    inline WiFiEventHandler onStationModeGotIP(ESP8266WiFiClass& _WiFi, std::function<void(const WiFiEventStationModeGotIP&)> f) {
      return _WiFi.onStationModeGotIP(f);
    }
    inline WiFiEventHandler onStationModeDisconnected(ESP8266WiFiClass& _WiFi, std::function<void(const WiFiEventStationModeDisconnected&)> f) {
      return _WiFi.onStationModeDisconnected(f);
    }
  #endif

  // UDP
  template<typename T>
  inline size_t writeRaw(WiFiUDP& udp, const T* buffer, size_t size) {
    #ifdef ESP32
      return udp.write((uint8_t*)(void*)buffer, size);
    #else
      return udp.write((char*)(void*)buffer, size);
    #endif
  }
  template<typename T>
  inline size_t writeT(WiFiUDP& udp, const T* arr, size_t count = 1) {
    #ifdef ESP32
      return udp.write((uint8_t*)(void*)arr, sizeof(T) * count);
    #else
      return udp.write((char*)(void*)arr, sizeof(T) * count);
    #endif
  }
}

#endif
