#include "InfoScreen.h"
#include "qrcode.h"

static inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

void InfoScreen::show(TFT_eSPI &tft, const char *title, const char *qrData,
                      const String &l1, const String &l2, const String &l3) {
  const uint16_t BG     = rgb565(0x11, 0x13, 0x18);   // #111318
  const uint16_t ACCENT = rgb565(0xd9, 0x77, 0x57);   // #d97757
  const uint16_t MUTED  = rgb565(0x8b, 0x8f, 0x9a);

  tft.fillScreen(BG);

  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(ACCENT, BG);
  tft.drawString(title, tft.width() / 2, 10, 4);

  // QR (version 3 = 29 modules, holds our short strings at ECC_LOW)
  QRCode qr;
  uint8_t buf[qrcode_getBufferSize(3)];
  qrcode_initText(&qr, buf, 3, ECC_LOW, qrData);

  const int scale = 6;                       // 29 * 6 = 174 px
  const int qw = qr.size * scale;
  const int qx = (tft.width() - qw) / 2;
  const int qy = 48;
  tft.fillRect(qx - 8, qy - 8, qw + 16, qw + 16, TFT_WHITE);  // quiet zone
  for (uint8_t y = 0; y < qr.size; y++)
    for (uint8_t x = 0; x < qr.size; x++)
      if (qrcode_getModule(&qr, x, y))
        tft.fillRect(qx + x * scale, qy + y * scale, scale, scale, TFT_BLACK);

  int ty = qy + qw + 18;
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(TFT_WHITE, BG);
  if (l1.length()) { tft.drawString(l1, tft.width() / 2, ty, 2); ty += 22; }
  if (l2.length()) { tft.drawString(l2, tft.width() / 2, ty, 2); ty += 22; }
  tft.setTextColor(MUTED, BG);
  if (l3.length()) { tft.drawString(l3, tft.width() / 2, ty, 2); }
}
