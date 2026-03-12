#include "esp_camera.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include "board_config.h"

// ============================================================
//  SOZLAMALAR
// ============================================================
#define WIFI_SSID           "WIFI SSID"
#define WIFI_PASSWORD       "WIFI PASS"
#define BOT_TOKEN           "BOT_TOKEN"
#define CHAT_ID             "USER_ID"

#define BOT_POLL_MS         1500
#define WIFI_TIMEOUT_MS     15000
#define PHOTO_QUALITY       10
#define MOTION_INTERVAL_MS  5000
#define MOTION_THRESHOLD    20000

#define TG_HOST             "api.telegram.org"
#define TG_PORT             443
#define TG_TIMEOUT_MS       10000
// ============================================================

// ---------- Holat ----------
static bool     s_flashOn    = false;
static bool     s_motionOn   = false;
static uint32_t s_lastPoll   = 0;
static uint32_t s_lastMotion = 0;
static int32_t  s_lastUpdate = 0;   // oxirgi qayta ishlangan update_id

// ============================================================
//  SSL ULANISH YORDAMCHI
// ============================================================
static WiFiClientSecure tgClient;

static bool tgConnect() {
  tgClient.setInsecure();           // Sertifikat tekshiruvsiz (sodda, ESP32 uchun yetarli)
  tgClient.setTimeout(TG_TIMEOUT_MS / 1000);
  if (!tgClient.connect(TG_HOST, TG_PORT)) {
    Serial.println("[TG] Ulanib bo'lmadi!");
    return false;
  }
  return true;
}

// HTTP so'rov yuborish va javob olish
// path: masalan "/bot<TOKEN>/sendMessage"
// body: POST body (bo'sh bo'lsa GET)
static String tgRequest(const String& path, const String& body = "",
                         const String& contentType = "application/json") {
  if (!tgConnect()) return "";

  if (body.length() > 0) {
    tgClient.printf("POST %s HTTP/1.1\r\n", path.c_str());
    tgClient.printf("Host: %s\r\n", TG_HOST);
    tgClient.printf("Content-Type: %s\r\n", contentType.c_str());
    tgClient.printf("Content-Length: %d\r\n", body.length());
    tgClient.println("Connection: close\r\n");
    tgClient.print(body);
  } else {
    tgClient.printf("GET %s HTTP/1.1\r\n", path.c_str());
    tgClient.printf("Host: %s\r\n", TG_HOST);
    tgClient.println("Connection: close\r\n");
  }

  // HTTP header ni o'tkazib yuborish
  uint32_t t0 = millis();
  while (tgClient.connected()) {
    if (millis() - t0 > TG_TIMEOUT_MS) break;
    String line = tgClient.readStringUntil('\n');
    if (line == "\r") break;
  }

  // Body ni o'qish
  String resp = "";
  t0 = millis();
  while (tgClient.connected() || tgClient.available()) {
    if (millis() - t0 > TG_TIMEOUT_MS) break;
    if (tgClient.available()) resp += (char)tgClient.read();
  }
  tgClient.stop();
  return resp;
}

// ============================================================
//  TELEGRAM API FUNKSIYALARI
// ============================================================

// Matn xabar yuborish
static bool tgSendMessage(const String& chat_id, const String& text,
                           const String& parseMode = "") {
  String path = "/bot" BOT_TOKEN "/sendMessage";
  StaticJsonDocument<512> doc;
  doc["chat_id"]    = chat_id;
  doc["text"]       = text;
  if (parseMode.length()) doc["parse_mode"] = parseMode;
  String body;
  serializeJson(doc, body);

  String resp = tgRequest(path, body);
  bool ok = resp.indexOf("\"ok\":true") >= 0;
  Serial.printf("[TG] sendMessage: %s\n", ok ? "OK" : "FAIL");
  return ok;
}

// Rasm yuborish (multipart/form-data)
static bool tgSendPhoto(const String& chat_id, camera_fb_t *fb,
                         const String& caption = "") {
  if (!tgConnect()) return false;

  String boundary = "ESP32CAMboundary";
  String path = "/bot" BOT_TOKEN "/sendPhoto";

  // Multipart header qismlari
  String partHead = "--" + boundary + "\r\n";
  partHead += "Content-Disposition: form-data; name=\"chat_id\"\r\n\r\n";
  partHead += chat_id + "\r\n";

  if (caption.length()) {
    partHead += "--" + boundary + "\r\n";
    partHead += "Content-Disposition: form-data; name=\"caption\"\r\n\r\n";
    partHead += caption + "\r\n";
  }

  String imgHead = "--" + boundary + "\r\n";
  imgHead += "Content-Disposition: form-data; name=\"photo\"; filename=\"cam.jpg\"\r\n";
  imgHead += "Content-Type: image/jpeg\r\n\r\n";

  String tail = "\r\n--" + boundary + "--\r\n";

  int totalLen = partHead.length() + imgHead.length() + fb->len + tail.length();

  // HTTP Header
  tgClient.printf("POST %s HTTP/1.1\r\n", path.c_str());
  tgClient.printf("Host: %s\r\n", TG_HOST);
  tgClient.printf("Content-Type: multipart/form-data; boundary=%s\r\n", boundary.c_str());
  tgClient.printf("Content-Length: %d\r\n", totalLen);
  tgClient.println("Connection: close\r\n");

  // Body
  tgClient.print(partHead);
  tgClient.print(imgHead);

  // Rasm ma'lumotlarini chunk usulida yuborish (RAM tejash)
  const size_t CHUNK = 1024;
  size_t sent = 0;
  while (sent < fb->len) {
    size_t toSend = min(CHUNK, fb->len - sent);
    tgClient.write(fb->buf + sent, toSend);
    sent += toSend;
    yield();  // WDT reset
  }

  tgClient.print(tail);

  // Javob o'qish
  uint32_t t0 = millis();
  while (tgClient.connected()) {
    if (millis() - t0 > TG_TIMEOUT_MS) break;
    String line = tgClient.readStringUntil('\n');
    if (line == "\r") break;
  }
  String resp = "";
  t0 = millis();
  while (tgClient.connected() || tgClient.available()) {
    if (millis() - t0 > TG_TIMEOUT_MS) break;
    if (tgClient.available()) resp += (char)tgClient.read();
  }
  tgClient.stop();

  bool ok = resp.indexOf("\"ok\":true") >= 0;
  Serial.printf("[TG] sendPhoto: %s (%zu bytes)\n", ok ? "OK" : "FAIL", fb->len);
  return ok;
}

// Yangi xabarlarni olish (getUpdates)
// Qaytaradi: JsonDocument ichida "result" array
static String tgGetUpdates(int32_t offset) {
  String path = "/bot" BOT_TOKEN "/getUpdates?timeout=1&limit=5&offset=";
  path += String(offset);
  return tgRequest(path);
}

// ============================================================
//  KAMERA
// ============================================================
static bool cameraInit() {
  camera_config_t cfg = {};
  cfg.ledc_channel = LEDC_CHANNEL_0; cfg.ledc_timer = LEDC_TIMER_0;
  cfg.pin_d0 = Y2_GPIO_NUM; cfg.pin_d1 = Y3_GPIO_NUM;
  cfg.pin_d2 = Y4_GPIO_NUM; cfg.pin_d3 = Y5_GPIO_NUM;
  cfg.pin_d4 = Y6_GPIO_NUM; cfg.pin_d5 = Y7_GPIO_NUM;
  cfg.pin_d6 = Y8_GPIO_NUM; cfg.pin_d7 = Y9_GPIO_NUM;
  cfg.pin_xclk = XCLK_GPIO_NUM; cfg.pin_pclk = PCLK_GPIO_NUM;
  cfg.pin_vsync = VSYNC_GPIO_NUM; cfg.pin_href = HREF_GPIO_NUM;
  cfg.pin_sccb_sda = SIOD_GPIO_NUM; cfg.pin_sccb_scl = SIOC_GPIO_NUM;
  cfg.pin_pwdn = PWDN_GPIO_NUM; cfg.pin_reset = RESET_GPIO_NUM;
  cfg.xclk_freq_hz = 20000000;
  cfg.pixel_format = PIXFORMAT_JPEG;
  cfg.grab_mode    = CAMERA_GRAB_LATEST;

  if (psramFound()) {
    cfg.frame_size   = FRAMESIZE_VGA;
    cfg.jpeg_quality = PHOTO_QUALITY;
    cfg.fb_count     = 2;
    cfg.fb_location  = CAMERA_FB_IN_PSRAM;
  } else {
    cfg.frame_size   = FRAMESIZE_SVGA;
    cfg.jpeg_quality = 12;
    cfg.fb_count     = 1;
    cfg.fb_location  = CAMERA_FB_IN_DRAM;
  }

  if (esp_camera_init(&cfg) != ESP_OK) return false;

  sensor_t *s = esp_camera_sensor_get();
  if (s->id.PID == OV3660_PID) {
    s->set_vflip(s, 1);
    s->set_brightness(s, 1);
    s->set_saturation(s, -2);
  }
  s->set_framesize(s, FRAMESIZE_QVGA);
  return true;
}

// ============================================================
//  FLASH
// ============================================================
static void flashSet(bool on) {
#if defined(LED_GPIO_NUM)
  digitalWrite(LED_GPIO_NUM, on ? HIGH : LOW);
#endif
  s_flashOn = on;
}

// ============================================================
//  RASM OLISH VA YUBORISH
// ============================================================
static bool sendPhoto(const String& chat_id, bool withFlash = false,
                       const String& caption = "") {
  if (withFlash) { flashSet(true); delay(150); }

  // Eski bufer kadrni tozalash
  camera_fb_t *dummy = esp_camera_fb_get();
  if (dummy) esp_camera_fb_return(dummy);
  delay(50);

  camera_fb_t *fb = esp_camera_fb_get();
  if (withFlash) flashSet(false);

  if (!fb) {
    tgSendMessage(chat_id, "❌ Kamera xatosi!");
    return false;
  }

  bool ok = tgSendPhoto(chat_id, fb, caption);
  esp_camera_fb_return(fb);
  return ok;
}

// ============================================================
//  SIFAT
// ============================================================
struct ResOption { const char* cmd; framesize_t size; const char* label; };
static const ResOption RES_TABLE[] = {
  { "/res_qqvga", FRAMESIZE_QQVGA, "160x120"  },
  { "/res_qvga",  FRAMESIZE_QVGA,  "320x240"  },
  { "/res_vga",   FRAMESIZE_VGA,   "640x480"  },
  { "/res_svga",  FRAMESIZE_SVGA,  "800x600"  },
  { "/res_hd",    FRAMESIZE_HD,    "1280x720" },
};

static bool setResolution(const String& cmd) {
  sensor_t *s = esp_camera_sensor_get();
  for (auto& r : RES_TABLE) {
    if (cmd == r.cmd) { s->set_framesize(s, r.size); return true; }
  }
  return false;
}

// ============================================================
//  KOMANDANI QAYTA ISHLASH
// ============================================================
static void processMessage(const String& cid, String text, const String& from) {
  text.trim();
  Serial.printf("[BOT] '%s' <= %s\n", text.c_str(), from.c_str());

  if (text == "/start") {
    String m = "👋 Salom, " + from + "!\n\n";
    m += "🤖 ESP32-CAM Pro Bot\n\n";
    m += "📸 /photo — Rasm olish\n";
    m += "📸 /photoflash — Flash bilan rasm\n";
    m += "💡 /flash_on — Flash yoqish\n";
    m += "🔦 /flash_off — Flash o'chirish\n";
    m += "🎥 /motion_on — Harakat sensori\n";
    m += "⛔ /motion_off — Sensorni o'chirish\n\n";
    m += "🖼 Sifat:\n";
    m += "/res_qqvga — 160x120\n";
    m += "/res_qvga  — 320x240\n";
    m += "/res_vga   — 640x480\n";
    m += "/res_svga  — 800x600\n";
    m += "/res_hd    — 1280x720\n\n";
    m += "ℹ️ /status  — Qurilma holati\n";
    m += "🔄 /restart — Qayta ishga tushirish\n";
    tgSendMessage(cid, m);
  }
  else if (text == "/photo")      { sendPhoto(cid, false); }
  else if (text == "/photoflash") { sendPhoto(cid, true, "📸 Flash bilan"); }
  else if (text == "/flash_on")   { flashSet(true);  tgSendMessage(cid, "💡 Flash yoqildi"); }
  else if (text == "/flash_off")  { flashSet(false); tgSendMessage(cid, "🔦 Flash o'chirildi"); }
  else if (text == "/motion_on")  { s_motionOn = true;  tgSendMessage(cid, "🎥 Harakat sensori yoqildi"); }
  else if (text == "/motion_off") { s_motionOn = false; tgSendMessage(cid, "⛔ Harakat sensori o'chirildi"); }
  else if (text.startsWith("/res_")) {
    if (setResolution(text)) {
      String label = text;
      for (auto& r : RES_TABLE) if (text == r.cmd) label = r.label;
      tgSendMessage(cid, "✅ Sifat: " + label);
    } else {
      tgSendMessage(cid, "❓ Noto'g'ri sifat komandasi");
    }
  }
  else if (text == "/status") {
    String m = "📊 Qurilma holati\n\n";
    m += "📶 WiFi: "   + String(WiFi.SSID()) + "\n";
    m += "📡 Signal: " + String(WiFi.RSSI()) + " dBm\n";
    m += "🌐 IP: "     + WiFi.localIP().toString() + "\n";
    m += "💡 Flash: "  + String(s_flashOn  ? "✅ Yoqiq"  : "❌ O'chiq") + "\n";
    m += "🎥 Sensor: " + String(s_motionOn ? "✅ Faol"   : "❌ Nofaol") + "\n";
    m += "🧠 PSRAM: "  + String(psramFound()? "✅ Bor"   : "❌ Yo'q")   + "\n";
    m += "⏱ Uptime: "  + String(millis()/1000) + " s\n";
    m += "🔋 Heap: "   + String(ESP.getFreeHeap()/1024) + " KB\n";
    tgSendMessage(cid, m);
  }
  else if (text == "/restart") {
    tgSendMessage(cid, "🔄 Qayta ishga tushirilmoqda...");
    delay(500);
    ESP.restart();
  }
  else {
    tgSendMessage(cid, "❓ Noma'lum komanda\n/start — ro'yxat");
  }
}

// ============================================================
//  POLLING — getUpdates parse qilish
// ============================================================
static void pollTelegram() {
  String resp = tgGetUpdates(s_lastUpdate + 1);
  if (resp.length() == 0) return;

  // JSON parse
  DynamicJsonDocument doc(4096);
  DeserializationError err = deserializeJson(doc, resp);
  if (err || !doc["ok"].as<bool>()) {
    Serial.printf("[TG] JSON xatosi: %s\n", err.c_str());
    return;
  }

  JsonArray results = doc["result"].as<JsonArray>();
  for (JsonObject upd : results) {
    int32_t uid = upd["update_id"].as<int32_t>();
    if (uid <= s_lastUpdate) continue;  // Eski xabarni o'tkazib yuborish
    s_lastUpdate = uid;

    if (upd.containsKey("message")) {
      JsonObject msg = upd["message"];
      String cid  = msg["chat"]["id"].as<String>();
      String text = msg["text"]      | "";
      String from = msg["from"]["first_name"] | "Noma'lum";
      if (text.length()) processMessage(cid, text, from);
    }
  }
}

// ============================================================
//  HARAKAT SENSORI
// ============================================================
static uint8_t *s_prevFrame    = nullptr;
static size_t   s_prevFrameLen = 0;

static void checkMotion() {
  if (!s_motionOn) return;
  if (millis() - s_lastMotion < MOTION_INTERVAL_MS) return;
  s_lastMotion = millis();

  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) return;

  if (s_prevFrame && fb->len == s_prevFrameLen) {
    long diff = 0;
    size_t step = max((size_t)1, fb->len / 500);
    for (size_t j = 0; j < fb->len; j += step)
      diff += abs((int)fb->buf[j] - (int)s_prevFrame[j]);

    if (diff > MOTION_THRESHOLD) {
      Serial.printf("[MOTION] diff=%ld\n", diff);
      memcpy(s_prevFrame, fb->buf, fb->len);
      esp_camera_fb_return(fb);
      tgSendMessage(String(CHAT_ID), "🚨 Harakat aniqlandi!");
      sendPhoto(String(CHAT_ID), false, "🚨 Harakat");
      return;
    }
  } else {
    if (s_prevFrame) free(s_prevFrame);
    s_prevFrame    = (uint8_t*)malloc(fb->len);
    s_prevFrameLen = fb->len;
  }

  if (s_prevFrame) memcpy(s_prevFrame, fb->buf, fb->len);
  esp_camera_fb_return(fb);
}

// ============================================================
//  SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  Serial.println("\n╔══════════════════════════╗");
  Serial.println("║  ESP32-CAM Telegram Bot  ║");
  Serial.println("║  (www.robotsoz.uz | DIN) ║");
  Serial.println("╚══════════════════════════╝");

#if defined(LED_GPIO_NUM)
  pinMode(LED_GPIO_NUM, OUTPUT);
  digitalWrite(LED_GPIO_NUM, LOW);
#endif

  if (!cameraInit()) {
    Serial.println("[ERR] Kamera xatosi! Restart...");
    delay(3000); ESP.restart();
  }

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  WiFi.setSleep(false);
  Serial.print("[NET] WiFi");
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - t0 > WIFI_TIMEOUT_MS) {
      Serial.println(" TIMEOUT! Restart...");
      delay(1000); ESP.restart();
    }
    delay(300); Serial.print(".");
  }
  Serial.printf(" OK | IP: %s\n", WiFi.localIP().toString().c_str());

  // NTP (SSL uchun to'g'ri vaqt kerak)
  configTime(5 * 3600, 0, "pool.ntp.org", "time.google.com");
  Serial.print("[NTP]");
  time_t now = time(nullptr);
  for (int i = 0; now < 100000 && i < 40; i++) {
    delay(200); Serial.print("."); now = time(nullptr);
  }
  Serial.println(" OK");

  // Eski xabarlarni o'tkazib yuborish uchun offset olish
  {
    String resp = tgGetUpdates(-1);  // oxirgi 1 ta update
    DynamicJsonDocument doc(1024);
    if (!deserializeJson(doc, resp) && doc["ok"].as<bool>()) {
      JsonArray arr = doc["result"].as<JsonArray>();
      for (JsonObject u : arr) s_lastUpdate = u["update_id"].as<int32_t>();
    }
    Serial.printf("[BOT] lastUpdate: %d\n", s_lastUpdate);
  }

  // Tayyor xabari
  String msg = "🟢 ESP32-CAM Bot tayyor!\n";
  msg += "IP: " + WiFi.localIP().toString() + "\n";
  msg += "PSRAM: " + String(psramFound() ? "✅" : "❌") + "\n";
  msg += "/start — komandalar ro'yxati";
  tgSendMessage(String(CHAT_ID), msg);

  Serial.println("[BOT] Tayyor!");
}

// ============================================================
//  LOOP
// ============================================================
void loop() {
  if (millis() - s_lastPoll > BOT_POLL_MS) {
    s_lastPoll = millis();
    pollTelegram();
  }

  checkMotion();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[NET] Uzildi, qayta ulanmoqda...");
    WiFi.reconnect();
    delay(5000);
  }
}
