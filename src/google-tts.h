#ifndef GoogleTTS_h
#define GoogleTTS_h

#include <ctype.h>
#include <string>

#define TTS_LIB_NAME "GoogleTTS for ESP32"
#define TTS_LIB_VERSION "1.1.0"

#define HOST_GTRANS "translate.google.com"
#define PATH_GTRANS "/translate_tts"

class GoogleTTS {
private:
  std::string urlencode(std::string str);

public:
  void setWiFiClientSecure(void *pClient) {
    // this method does nothing, kept for interface compatibility if needed
    (void)pClient;
  }
  std::string getSpeechUrl(std::string text, std::string lang);
  std::string getSpeechUrl(std::string text) {
    return getSpeechUrl(text, "en");
  }
};

#endif
