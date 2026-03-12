# 📷 ESP32-CAM Telegram Bot

ESP32-CAM modulini Telegram bot orqali boshqarish imkonini beruvchi loyiha. Kameradan rasm olish, flash boshqaruvi, harakat aniqlash va qurilma holati kabi funksiyalar mavjud.

---

## 🚀 Imkoniyatlar

| Funksiya | Tavsif |
|---|---|
| 📸 Rasm olish | Telegram orqali kameradan on-demand rasm yuborish |
| 💡 Flash boshqaruvi | LED flashni yoqish/o'chirish |
| 🎥 Harakat sensori | Avtomatik harakat aniqlash va bildirishnoma yuborish |
| 🖼 Sifat sozlama | 160x120 dan 1280x720 gacha 5 xil o'lcham |
| 📊 Qurilma holati | WiFi, IP, heap, uptime ma'lumotlari |
| 🔄 Qayta ishga tushirish | Botdan turib ESP32 restart |

---

## 🛒 Kerakli Qurilmalar

- **ESP32-CAM** moduli (AI-Thinker yoki mos boshqalar)
- FTDI / USB-UART adapter (dasturlash uchun)
- Micro-USB kabel
- 5V quvvat manbai

---

## 📦 Kerakli Kutubxonalar

Arduino IDE da quyidagi kutubxonalarni o'rnating:

```
ArduinoJson       >= 6.x
esp32 board pack  >= 2.x  (Espressif Systems)
```

> **ESP32-CAM** uchun `esp_camera.h` board pack ichida keladi — alohida o'rnatish shart emas.

---

## ⚙️ Sozlash

`main.ino` (yoki `.cpp`) faylining yuqori qismidagi quyidagi qiymatlarni o'zgartiring:

```cpp
#define WIFI_SSID       "YOUR_WIFI_NAME"
#define WIFI_PASSWORD   "YOUR_WIFI_PASSWORD"
#define BOT_TOKEN       "YOUR_BOT_TOKEN"
#define CHAT_ID         "YOUR_CHAT_ID"
```

### Telegram Bot token olish
1. Telegram'da `@BotFather` ga yozing
2. `/newbot` komandasi bering
3. Nom va username kiriting
4. Olingan tokenni `BOT_TOKEN` ga joylashtiring

### Chat ID olish
1. `@userinfobot` ga `/start` yuboring
2. Olingan ID ni `CHAT_ID` ga joylashtiring

---

## 🔧 Qo'shimcha Sozlamalar

```cpp
#define BOT_POLL_MS         1500    // Telegram so'rovi oralig'i (ms)
#define PHOTO_QUALITY       10      // JPEG sifati (1=eng yaxshi, 63=eng past)
#define MOTION_INTERVAL_MS  5000    // Harakat tekshiruv oralig'i (ms)
#define MOTION_THRESHOLD    20000   // Harakat sezgirlik darajasi
```

---

## 📲 Bot Komandalar Ro'yxati

| Komanda | Tavsif |
|---|---|
| `/start` | Barcha komandalar ro'yxati |
| `/photo` | Oddiy rasm olish |
| `/photoflash` | Flash bilan rasm olish |
| `/flash_on` | Flashni yoqish |
| `/flash_off` | Flashni o'chirish |
| `/motion_on` | Harakat sensorini yoqish |
| `/motion_off` | Harakat sensorini o'chirish |
| `/status` | Qurilma holati |
| `/restart` | ESP32ni qayta ishga tushirish |
| `/res_qqvga` | Sifat: 160×120 |
| `/res_qvga` | Sifat: 320×240 |
| `/res_vga` | Sifat: 640×480 |
| `/res_svga` | Sifat: 800×600 |
| `/res_hd` | Sifat: 1280×720 |

---

## 🏗 Arxitektura

```
ESP32-CAM
├── WiFi ulanish (WPA2)
├── NTP vaqt sinxronizatsiyasi
├── Telegram API (HTTPS / SSL)
│   ├── getUpdates  → Polling (har 1.5s)
│   ├── sendMessage → Matn xabarlar
│   └── sendPhoto   → Multipart JPEG yuklash
├── Kamera drayveri (esp_camera)
│   ├── PSRAM mavjud bo'lsa → VGA + 2 bufer
│   └── PSRAM yo'q bo'lsa  → SVGA + 1 bufer
└── Harakat sensori
    └── Kadrlarni piksel-darajada solishtirish
```

---

## 📡 Ishlash Prinsipi

1. **Boot**: Kamera va WiFi ishga tushiriladi, NTP orqali vaqt olinadi
2. **Polling**: Har `BOT_POLL_MS` millisekundda `getUpdates` so'rovi yuboriladi
3. **Komanda**: Kelgan matn qayta ishlanib, mos funksiya chaqiriladi
4. **Harakat**: Ketma-ket kadrlar baytma-bayt solishtiriladi; farq `MOTION_THRESHOLD` dan oshsa — bildirishnoma va rasm yuboriladi

---

## ❗ Muhim Eslatmalar

- SSL sertifikati tekshiruvi **o'chirilgan** (`setInsecure()`) — mahalliy ishlatish uchun yetarli, production muhitda sertifikat pin qo'shish tavsiya etiladi
- Harakat sensori JPEG baytlarini solishtiradi — bu taxminiy usul, lekin ESP32 uchun optimal
- PSRAM bo'lmasa rasm sifati va o'lchami cheklanadi
- `CHAT_ID` noto'g'ri bo'lsa bot xabar yubora olmaydi

---

## 📁 Fayl Tuzilmasi

```
project/
├── main.ino (yoki main.cpp)
├── board_config.h      ← GPIO pin raqamlari
└── README.md
```

---

## 📜 Litsenziya

Ushbu loyiha ochiq manba asosida tarqatiladi.  
Muallif: **[www.robotsoz.uz](http://www.robotsoz.uz) | DIN**