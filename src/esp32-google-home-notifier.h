#ifndef GoogleHomeNotifier_h
#define GoogleHomeNotifier_h

#include "cast_channel.pb.h"
#include "pb.h"
#include "pb_decode.h"
#include "pb_encode.h"

#include <cstring>
#include <string>
#include <vector>

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_tls.h"
#include "mdns.h"

#include "google-tts.h"

#define GHN_LIB_NAME "GoogleHomeNotifier for ESP32-IDF"
#define GHN_LIB_VERSION "0.0.1"

#define APP_ID "CC1AD845"

#define SOURCE_ID "sender-0"
#define DESTINATION_ID "receiver-0"

#define CASTV2_NS_CONNECTION "urn:x-cast:com.google.cast.tp.connection"
#define CASTV2_NS_HEARTBEAT "urn:x-cast:com.google.cast.tp.heartbeat"
#define CASTV2_NS_RECEIVER "urn:x-cast:com.google.cast.receiver"
#define CASTV2_NS_MEDIA "urn:x-cast:com.google.cast.media"

#define CASTV2_DATA_CONNECT "{\"type\":\"CONNECT\"}"
#define CASTV2_DATA_PING "{\"type\":\"PING\"}"
#define CASTV2_DATA_LAUNCH                                                     \
  "{\"type\":\"LAUNCH\",\"appId\":\"%s\",\"requestId\":1}"
#define CASTV2_DATA_LOAD                                                       \
  "{\"type\":\"LOAD\",\"autoplay\":true,\"currentTime\":0,\"activeTrackIds\":" \
  "[],\"repeatMode\":\"REPEAT_OFF\",\"media\":{\"contentId\":\"%s\","          \
  "\"contentType\":\"audio/"                                                   \
  "mp3\",\"streamType\":\"BUFFERED\"},\"requestId\":1}"

// Simple IPAddress compatibility to minimize rewrite
struct IPAddress {
  uint8_t bytes[4];
  IPAddress() {
    bytes[0] = 0;
    bytes[1] = 0;
    bytes[2] = 0;
    bytes[3] = 0;
  }
  IPAddress(uint32_t address) {
    bytes[0] = (uint8_t)(address);
    bytes[1] = (uint8_t)(address >> 8);
    bytes[2] = (uint8_t)(address >> 16);
    bytes[3] = (uint8_t)(address >> 24);
  }
  IPAddress(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
    bytes[0] = a;
    bytes[1] = b;
    bytes[2] = c;
    bytes[3] = d;
  }
  uint8_t operator[](int index) const { return bytes[index]; }
  uint8_t &operator[](int index) { return bytes[index]; }

  std::string toString() const {
    char buf[16];
    snprintf(buf, sizeof(buf), "%d.%d.%d.%d", bytes[0], bytes[1], bytes[2],
             bytes[3]);
    return std::string(buf);
  }
};

class GoogleHomeNotifier {

private:
  char m_transportid[40] = {0};
  char m_clientid[40] = {0};

  GoogleTTS tts;

  // ESP-TLS connection handle
  esp_tls_t *m_tls = nullptr;

  IPAddress m_ipaddress;
  uint16_t m_port = 0;
  char m_locale[10] = "en";
  char m_name[128] = "";
  char m_lastError[128] = "";

  static bool encode_string(pb_ostream_t *stream, const pb_field_t *field,
                            void *const *arg);
  static bool decode_string(pb_istream_t *stream, const pb_field_t *field,
                            void **arg);

  bool connect();
  bool _play(const char *mp3url);
  void disconnect();
  void setLastError(const char *lastError);
  bool sendMessage(const char *sourceId, const char *destinationId,
                   const char *ns, const char *data);
  bool cast(const char *phrase, const char *mp3Url);

public:
  GoogleHomeNotifier();
  ~GoogleHomeNotifier();

  bool ip(IPAddress ip, const char *locale = "en", uint16_t port = 8009);
  bool device(const char *name, const char *locale = "en",
              int timeout_ms = 10000);
  bool notify(const char *phrase); // Removed WiFiClient arg
  bool play(const char *mp3Url);   // Removed WiFiClient arg

  IPAddress getIPAddress();
  uint16_t getPort();
  const char *getLastError();
};

#endif
