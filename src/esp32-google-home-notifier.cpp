#include "esp32-google-home-notifier.h"
#include <algorithm>
#include <esp_err.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <lwip/inet.h>

static const char *TAG = "GHNotifier";

static uint32_t millis() { return (uint32_t)(esp_timer_get_time() / 1000ULL); }

static void delay(uint32_t ms) { vTaskDelay(pdMS_TO_TICKS(ms > 0 ? ms : 1)); }

GoogleHomeNotifier::GoogleHomeNotifier() {}

GoogleHomeNotifier::~GoogleHomeNotifier() { disconnect(); }

bool GoogleHomeNotifier::ip(IPAddress ip, const char *locale, uint16_t port) {
  this->m_ipaddress = ip;
  this->m_port = port;
  snprintf(this->m_locale, sizeof(this->m_locale), "%s", locale);
  return true;
}

bool GoogleHomeNotifier::device(const char *name, const char *locale,
                                int timeout_ms) {
  snprintf(this->m_locale, sizeof(this->m_locale), "%s", locale);

  // If already set to this name, maybe verify IP? For now assume re-discovery
  // needed if name differs or just force it. If name matches existing, we could
  // skip, but dynamic IP storage suggests re-discovery is safer.

  ESP_LOGI(TAG, "Searching for Google Home: %s", name);

  // Check if name is an IP address
  ip_addr_t ip_check;
  if (ipaddr_aton(name, &ip_check)) {
    if (IP_IS_V4(&ip_check)) {
      uint32_t ip_u32 = ip4_addr_get_u32(ip_2_ip4(&ip_check));
      this->m_ipaddress = IPAddress(ip_u32);
      this->m_port = 8009; // Default Google Cast port
      snprintf(this->m_name, sizeof(this->m_name), "%s", name);
      ESP_LOGI(TAG, "Using direct IP: %s (Port: %d)",
               m_ipaddress.toString().c_str(), m_port);
      return true;
    }
  }

  mdns_result_t *results = NULL;
  esp_err_t err =
      mdns_query_ptr("_googlecast", "_tcp", timeout_ms, 20, &results);
  if (err) {
    setLastError("mDNS query failed");
    return false;
  }
  if (!results) {
    setLastError("No Google Cast devices found");
    return false;
  }

  bool found = false;
  mdns_result_t *r = results;
  while (r) {
    // Check TXT for "fn" (Friendly Name)
    const char *fn = NULL;
    for (size_t i = 0; i < r->txt_count; i++) {
      if (strcmp(r->txt[i].key, "fn") == 0) {
        fn = r->txt[i].value;
        break;
      }
    }

    if (fn && strcmp(fn, name) == 0) {
      // Match found!
      if (r->addr) {
        // Use first IPv4
        mdns_ip_addr_t *a = r->addr;
        while (a) {
          if (a->addr.type == ESP_IPADDR_TYPE_V4) {
            uint32_t ip_u32 = a->addr.u_addr.ip4.addr; // ESP-IP is uint32_t
            this->m_ipaddress = IPAddress(ip_u32);
            this->m_port = r->port;
            snprintf(this->m_name, sizeof(this->m_name), "%s", name);
            found = true;
            ESP_LOGI(TAG, "Found Device: %s at %s:%d", name,
                     m_ipaddress.toString().c_str(), m_port);
            break;
          }
          a = a->next;
        }
      }
    }
    if (found)
      break;
    r = r->next;
  }

  mdns_query_results_free(results);

  if (!found) {
    setLastError("Device not found by name");
    return false;
  }
  return true;
}

bool GoogleHomeNotifier::notify(const char *phrase) {
  return this->cast(phrase, nullptr);
}

bool GoogleHomeNotifier::play(const char *mp3Url) {
  return this->cast(nullptr, mp3Url);
}

bool GoogleHomeNotifier::cast(const char *phrase, const char *mp3Url) {
  if (m_ipaddress[0] == 0 && m_ipaddress[1] == 0 && m_ipaddress[2] == 0 &&
      m_ipaddress[3] == 0) {
    setLastError("IP not set. Call device() first.");
    return false;
  }

  std::string speechUrl;
  if (phrase != nullptr) {
    std::string s_phrase = phrase;
    std::string s_locale = m_locale;
    speechUrl = tts.getSpeechUrl(s_phrase, s_locale);
    if (speechUrl.find("https://") != 0) {
      setLastError("Failed to get TTS URL");
      return false;
    }
  } else if (mp3Url != nullptr) {
    speechUrl = mp3Url;
  } else {
    setLastError("No phrase or URL provided");
    return false;
  }

  // Connect via ESP-TLS
  disconnect(); // Close existing

  m_tls = esp_tls_init();
  if (!m_tls) {
    setLastError("Failed to init ESP-TLS");
    return false;
  }

  esp_tls_cfg_t cfg = {};
  cfg.common_name = NULL;
  cfg.skip_common_name = true; // Trust IP connection
  cfg.non_block = false;

  char host_str[32];
  snprintf(host_str, sizeof(host_str), "%d.%d.%d.%d", m_ipaddress[0],
           m_ipaddress[1], m_ipaddress[2], m_ipaddress[3]);

  ESP_LOGI(TAG, "Connecting to %s:%d", host_str, m_port);
  int ret =
      esp_tls_conn_new_sync(host_str, strlen(host_str), m_port, &cfg, m_tls);
  if (ret != 1) { // 1 means success
    setLastError("TLS Connection failed");
    disconnect();
    return false;
  }

  delay(10);
  if (!this->connect()) {
    char errBuf[128];
    snprintf(errBuf, sizeof(errBuf), "Failed to Open-Session: %s",
             getLastError());
    setLastError(errBuf);
    disconnect();
    return false;
  }

  delay(10);
  if (!this->_play(speechUrl.c_str())) {
    char errBuf[128];
    snprintf(errBuf, sizeof(errBuf), "Failed to play: %s", getLastError());
    setLastError(errBuf);
    disconnect();
    return false;
  }

  disconnect();
  return true;
}

void GoogleHomeNotifier::disconnect() {
  if (m_tls) {
    esp_tls_conn_destroy(m_tls);
    m_tls = nullptr;
  }
}

bool GoogleHomeNotifier::sendMessage(const char *sourceId,
                                     const char *destinationId, const char *ns,
                                     const char *data) {
  extensions_api_cast_channel_CastMessage message =
      extensions_api_cast_channel_CastMessage_init_default;

  message.protocol_version =
      extensions_api_cast_channel_CastMessage_ProtocolVersion_CASTV2_1_0;
  message.source_id.funcs.encode = &(GoogleHomeNotifier::encode_string);
  message.source_id.arg = (void *)sourceId;
  message.destination_id.funcs.encode = &(GoogleHomeNotifier::encode_string);
  message.destination_id.arg = (void *)destinationId;
  message.namespace_str.funcs.encode = &(GoogleHomeNotifier::encode_string);
  message.namespace_str.arg = (void *)ns;
  message.payload_type =
      extensions_api_cast_channel_CastMessage_PayloadType_STRING;
  message.payload_utf8.funcs.encode = &(GoogleHomeNotifier::encode_string);
  message.payload_utf8.arg = (void *)data;

  uint8_t
      scratch_buf[2048]; // Use stack buffer instead of dynamic alloc/realloc

  pb_ostream_t stream =
      pb_ostream_from_buffer(scratch_buf, sizeof(scratch_buf));
  if (!pb_encode(&stream, extensions_api_cast_channel_CastMessage_fields,
                 &message)) {
    setLastError("Protobuf encode failed");
    return false;
  }

  uint32_t bufferSize = stream.bytes_written;
  uint8_t header[4];
  for (int i = 0; i < 4; i++) {
    header[3 - i] = (bufferSize >> (8 * i)) & 0xFF;
  }

  // Write Header
  if (esp_tls_conn_write(m_tls, header, 4) < 0)
    return false;
  // Write Body
  if (esp_tls_conn_write(m_tls, scratch_buf, bufferSize) < 0)
    return false;

  // m_client->flush() equivalent? TCP stack handles it.
  delay(10);
  return true;
}

bool GoogleHomeNotifier::connect() {
  // send 'CONNECT'
  if (!sendMessage(SOURCE_ID, DESTINATION_ID, CASTV2_NS_CONNECTION,
                   CASTV2_DATA_CONNECT)) {
    setLastError("'CONNECT' message send failed");
    return false;
  }
  delay(10);

  // send 'PING'
  if (!sendMessage(SOURCE_ID, DESTINATION_ID, CASTV2_NS_HEARTBEAT,
                   CASTV2_DATA_PING)) {
    setLastError("'PING' message send failed");
    return false;
  }
  delay(10);

  // send 'LAUNCH'
  char launch_data[128];
  snprintf(launch_data, sizeof(launch_data), CASTV2_DATA_LAUNCH, APP_ID);
  if (!sendMessage(SOURCE_ID, DESTINATION_ID, CASTV2_NS_RECEIVER,
                   launch_data)) {
    setLastError("'LAUNCH' message send failed");
    return false;
  }
  delay(10);

  // Wait for response logic (Transport ID extraction)
  // Original code waited for read.
  // We implement a read loop with timeout.

  int timeout = millis() + 5000;
  while ((int)millis() < timeout) {
    // Read Header
    uint8_t header[4];
    ssize_t read_len = esp_tls_conn_read(m_tls, header, 4);

    if (read_len == ESP_TLS_ERR_SSL_WANT_READ ||
        read_len == ESP_TLS_ERR_SSL_WANT_WRITE) {
      delay(10);
      continue;
    }
    if (read_len <= 0) { // Error or closed
      // If nothing available yet, esp_tls might block or return specific error?
      // If configured non-blocking, it returns negative.
      // We configured blocking for simplicity in `new_sync`.
      // But Wait, `esp_tls_conn_read` blocks if sockets are blocking.
      // So loop handles logic.
      // If connection closes?
      if (read_len == 0 && (int)millis() < timeout) {
        delay(10);
        continue;
      } // Maybe partial?
      break;
    }

    uint32_t body_len = 0;
    for (int i = 0; i < 4; i++)
      body_len |= header[i] << (8 * (3 - i));

    if (body_len > 2048) {
      ESP_LOGW(TAG, "Message too large: %ld", (long)body_len);
      // Skip it? Can't skip comfortably.
      return false;
    }

    // Read Body
    uint8_t body[2048];
    size_t total_read = 0;
    int sub_timeout = millis() + 2000;
    while (total_read < body_len && (int)millis() < sub_timeout) {
      ssize_t r =
          esp_tls_conn_read(m_tls, body + total_read, body_len - total_read);
      if (r > 0)
        total_read += r;
      else
        delay(10);
    }
    if (total_read < body_len)
      break; // Failed

    // Decode
    extensions_api_cast_channel_CastMessage imsg =
        extensions_api_cast_channel_CastMessage_init_default;
    pb_istream_t istream = pb_istream_from_buffer(body, body_len);

    imsg.source_id.funcs.decode = &(GoogleHomeNotifier::decode_string);
    imsg.source_id.arg = (void *)"sid";
    imsg.destination_id.funcs.decode = &(GoogleHomeNotifier::decode_string);
    imsg.destination_id.arg = (void *)"did";
    imsg.namespace_str.funcs.decode = &(GoogleHomeNotifier::decode_string);
    imsg.namespace_str.arg = (void *)"ns";
    imsg.payload_utf8.funcs.decode = &(GoogleHomeNotifier::decode_string);
    // const char *payload_location = NULL; // Removed unused
    // decode_string allocates buffer? No, existing implementation used stack
    // buffer and cast void***. Let's look at decode_string.
    char decoded_payload_buf[1024] = {0};
    imsg.payload_utf8.arg =
        (void *)decoded_payload_buf; // We need to modify decode to fill this

    if (!pb_decode(&istream, extensions_api_cast_channel_CastMessage_fields,
                   &imsg)) {
      continue;
    }

    // Check content
    // In decode_string, we need to handle how it writes to arg.
    // Original: *arg = (void***)buffer; Wrong pointer syntax in original?
    // Let's fix decode_string below.

    std::string json(decoded_payload_buf);
    // Find transportId
    if (json.find("\"appId\":\"" APP_ID "\"") != std::string::npos) {
      size_t pos = json.find("\"transportId\":");
      if (pos != std::string::npos) {
        // e.g. "transportId":"web-43..."
        // Extract value.
        size_t start = json.find("\"", pos + 13) + 1;
        size_t end = json.find("\"", start);
        if (start != std::string::npos && end != std::string::npos) {
          std::string tid = json.substr(start, end - start);
          snprintf(m_transportid, sizeof(m_transportid), "%s", tid.c_str());
          snprintf(m_clientid, sizeof(m_clientid), "client-%lu",
                   (unsigned long)millis());
          return true; // Success!
        }
      }
    }
  }
  setLastError("Transport ID not found");
  return false;
}

bool GoogleHomeNotifier::_play(const char *mp3url) {
  if (!sendMessage(m_clientid, m_transportid, CASTV2_NS_CONNECTION,
                   CASTV2_DATA_CONNECT))
    return false;
  delay(10);

  char load_data[1024];
  snprintf(load_data, sizeof(load_data), CASTV2_DATA_LOAD, mp3url);
  if (!sendMessage(m_clientid, m_transportid, CASTV2_NS_MEDIA, load_data)) {
    setLastError("'LOAD' send failed");
    return false;
  }
  delay(10);
  setLastError("");
  return true;
}

bool GoogleHomeNotifier::encode_string(pb_ostream_t *stream,
                                       const pb_field_t *field,
                                       void *const *arg) {
  char *str = (char *)*arg;
  if (!pb_encode_tag_for_field(stream, field))
    return false;
  return pb_encode_string(stream, (uint8_t *)str, strlen(str));
}

bool GoogleHomeNotifier::decode_string(pb_istream_t *stream,
                                       const pb_field_t *field, void **arg) {
  // Original implementation:
  // uint8_t buffer[1024]; pb_read... *arg = buffer;
  // Issue: buffer is on stack, *arg points to it, function returns -> buffer
  // invalid! Original code was dangerous/buggy but maybe worked by luck or
  // weird compiler optimization? Correct way: arg should point to a buffer
  // provided by caller, OR we allocate valid memory. In our connect() code, we
  // set arg = decoded_payload_buf.

  char *dest = (char *)*arg; // We assume arg was set to a char* buffer
  if (!dest) {
    // Just skip if no buffer
    uint8_t dummy[64];
    while (stream->bytes_left) {
      size_t n = stream->bytes_left > sizeof(dummy) ? sizeof(dummy)
                                                    : stream->bytes_left;
      pb_read(stream, dummy, n);
    }
    return true;
  }

  if (stream->bytes_left > 1023)
    return false;

  if (!pb_read(stream, (uint8_t *)dest, stream->bytes_left))
    return false;
  dest[stream->bytes_left] = 0; // Null terminate

  return true;
}

const char *GoogleHomeNotifier::getLastError() { return m_lastError; }

void GoogleHomeNotifier::setLastError(const char *lastError) {
  snprintf(m_lastError, sizeof(m_lastError), "%s", lastError);
  ESP_LOGE(TAG, "%s", lastError);
}

IPAddress GoogleHomeNotifier::getIPAddress() { return m_ipaddress; }

uint16_t GoogleHomeNotifier::getPort() { return m_port; }
