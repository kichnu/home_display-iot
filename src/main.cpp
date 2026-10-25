// Display + touch bring-up test for Waveshare ESP32-S3-Touch-LCD-4.3B.
//
// Boot: I2C scan, CH422G init (backlight on), RGB panel init, GT911 init.
// Then a sequence of test patterns (tap the screen, send 'n' over Serial or wait
// PATTERN_TIMEOUT_MS to advance), followed by an interactive multi-touch paint screen.
//
// Serial commands: n = next pattern, p = restart patterns, t = touch screen, b = toggle backlight,
//                  s = I2C scan, d / f = DI0/DI1 read test every 200 / 20 ms with backlight off
//                  (any key or a touch ends it).
//
// DI test: CH422G has one IO direction for all eight lines, so reading DI0 (EXIO0) / DI1 (EXIO5)
// briefly turns every IO into an input. DISP is pulled up on the board, so the backlight may flash
// during each read while it is supposed to be off - watch the screen in a dark room.

#include <Arduino.h>
#include <Wire.h>
#include <esp_heap_caps.h>

#include "board_pins.h"
#include "ch422g.h"
#include "gt911.h"
#include "lcd.h"

namespace {

constexpr uint32_t PATTERN_TIMEOUT_MS = 5000;
constexpr uint32_t TOUCH_POLL_MS = 10;
constexpr uint32_t TOUCH_RELEASE_TIMEOUT_MS = 80;  // no new frame for this long = finger lifted
constexpr uint32_t HEADER_MIN_INTERVAL_MS = 50;
constexpr int HEADER_H = 64;
constexpr uint8_t EXIO_DI0 = 0;

enum class Mode { Patterns, Touch };

Mode g_mode = Mode::Patterns;
int g_pattern = 0;
uint32_t g_patternShownAt = 0;
bool g_touchOk = false;
bool g_backlight = true;
gt911::Info g_tpInfo = {};

// DI read test state
bool g_diTest = false;
uint32_t g_diIntervalMs = 200;
uint32_t g_diLastRead = 0, g_diLastReport = 0;
uint32_t g_diReads = 0, g_diErrors = 0, g_diMaxUs = 0, g_diSumUs = 0;
int g_diLast = -1;
uint32_t g_tpErrorsAtDiStart = 0;

// Touch state
gt911::Point g_pts[gt911::MAX_POINTS];
int g_lastCount = 0;
int16_t g_lastX[16], g_lastY[16];
uint32_t g_lastPoll = 0;
uint32_t g_readsInSecond = 0, g_readsPerSecond = 0, g_rateWindowStart = 0;
uint32_t g_touchErrors = 0;
uint32_t g_lastFrameAt = 0;

// Header is rendered off-screen and copied in one go to avoid visible flicker.
uint16_t* g_hdrBuf = nullptr;
bool g_headerDirty = false;
uint32_t g_lastHeaderAt = 0;
char g_lastHeaderText[160] = "";

const uint16_t TRAIL_COLORS[] = {lcd::RED, lcd::GREEN, lcd::rgb(60, 140, 255), lcd::YELLOW,
                                 lcd::MAGENTA, lcd::CYAN, lcd::WHITE, lcd::rgb(255, 128, 0)};

struct Button {
    int x, y, w, h;
    const char* label;
    bool hit(int px, int py) const { return px >= x && px < x + w && py >= y && py < y + h; }
};
const Button BTN_CLEAR = {LCD_W - 250, 8, 110, 48, "CLEAR"};
const Button BTN_PATTERNS = {LCD_W - 130, 8, 122, 48, "PATTERNS"};

// ---------------------------------------------------------------------------
// I2C helpers

void i2cScan() {
    Serial.println("I2C scan:");
    int found = 0;
    for (uint8_t a = 1; a < 0x7F; a++) {
        Wire.beginTransmission(a);
        if (Wire.endTransmission() == 0) {
            const char* hint = "";
            if (a == 0x5D || a == 0x14) hint = " (GT911 touch)";
            else if (a == 0x51) hint = " (PCF85063 RTC)";
            else if (a == 0x24 || a == 0x38 || a == 0x26 || a == 0x23) hint = " (CH422G command)";
            else if ((a >= 0x20 && a <= 0x27) || (a >= 0x30 && a <= 0x3F)) hint = " (CH422G range)";
            Serial.printf("  0x%02X%s\n", a, hint);
            found++;
        }
    }
    Serial.printf("  %d device(s)\n", found);
}

// ---------------------------------------------------------------------------
// Test patterns

void drawCaption(const char* s) {
    int w = lcd::textWidth(s, 2) + 16;
    lcd::fillRect(0, LCD_H - 24, w, 24, lcd::BLACK);
    lcd::text(8, LCD_H - 20, s, lcd::WHITE, lcd::BLACK, 2);
}

void patternSolid(uint16_t color, const char* name) {
    lcd::fill(color);
    drawCaption(name);
}

void patternColorBars() {
    const uint16_t bars[] = {lcd::WHITE, lcd::YELLOW, lcd::CYAN,    lcd::GREEN,
                             lcd::MAGENTA, lcd::RED,  lcd::BLUE,    lcd::BLACK};
    const int n = sizeof(bars) / sizeof(bars[0]);
    for (int i = 0; i < n; i++) {
        int x0 = i * LCD_W / n, x1 = (i + 1) * LCD_W / n;
        lcd::fillRect(x0, 0, x1 - x0, LCD_H, bars[i]);
    }
    drawCaption("Color bars");
}

// Four horizontal bands: R (32 steps), G (64 steps), B (32 steps), gray (32 steps).
// Missing steps / banding reveal a dead or swapped data line.
void patternGradients() {
    const int band = LCD_H / 4;
    for (int x = 0; x < LCD_W; x++) {
        uint8_t r5 = x * 32 / LCD_W, g6 = x * 64 / LCD_W, b5 = x * 32 / LCD_W;
        lcd::vline(x, 0 * band, band, r5 << 11);
        lcd::vline(x, 1 * band, band, g6 << 5);
        lcd::vline(x, 2 * band, band, b5);
        lcd::vline(x, 3 * band, band, (r5 << 11) | ((r5 << 1) << 5) | r5);
    }
    drawCaption("Gradients R/G/B/gray");
}

// 16 stripes, each with exactly one RGB565 data bit set (D15..D0 left to right).
// Every stripe must be visible and distinct; a black stripe = dead line.
void patternBitWalk() {
    static const char* names[16] = {"R7", "R6", "R5", "R4", "R3", "G7", "G6", "G5",
                                    "G4", "G3", "G2", "B7", "B6", "B5", "B4", "B3"};
    lcd::fill(lcd::BLACK);
    const int w = LCD_W / 16;
    for (int i = 0; i < 16; i++) {
        uint16_t color = 1u << (15 - i);
        lcd::fillRect(i * w, 40, w - 2, LCD_H - 90, color);
        lcd::text(i * w + (w - 24) / 2, 12, names[i], lcd::WHITE, lcd::BLACK, 2);
    }
    drawCaption("Bit walk: one data line per stripe");
}

// 1 px border, 50 px grid, centre cross and circles: checks timing, geometry and clipping.
void patternGeometry() {
    lcd::fill(lcd::BLACK);
    for (int x = 0; x < LCD_W; x += 50) lcd::vline(x, 0, LCD_H, lcd::DARKGRAY);
    for (int y = 0; y < LCD_H; y += 50) lcd::hline(0, y, LCD_W, lcd::DARKGRAY);
    lcd::rect(0, 0, LCD_W, LCD_H, lcd::WHITE);
    lcd::rect(2, 2, LCD_W - 4, LCD_H - 4, lcd::RED);
    lcd::hline(LCD_W / 2 - 40, LCD_H / 2, 80, lcd::GREEN);
    lcd::vline(LCD_W / 2, LCD_H / 2 - 40, 80, lcd::GREEN);
    lcd::circle(LCD_W / 2, LCD_H / 2, 6, lcd::GREEN);
    for (int r = 100; r <= 220; r += 60) {
        for (int a = 0; a < 360; a++) {
            float rad = a * PI / 180.0f;
            lcd::pixel(LCD_W / 2 + r * cosf(rad), LCD_H / 2 + r * sinf(rad), lcd::YELLOW);
        }
    }
    lcd::text(10, 10, "0,0", lcd::WHITE, lcd::WHITE, 2);
    lcd::text(LCD_W - 10 - lcd::textWidth("799,479", 2), LCD_H - 30, "799,479", lcd::WHITE,
              lcd::WHITE, 2);
    // Fine 1-px checkerboard patch: shimmer here hints at PCLK / timing problems.
    for (int y = 0; y < 80; y++)
        for (int x = 0; x < 120; x++)
            lcd::pixel(60 + x, 300 + y, ((x ^ y) & 1) ? lcd::WHITE : lcd::BLACK);
    drawCaption("Geometry: border, 50px grid, circles");
}

void patternInfo() {
    lcd::fill(lcd::rgb(10, 20, 40));
    char buf[96];
    int y = 30;
    lcd::text(30, y, "ESP32-S3-Touch-LCD-4.3B", lcd::WHITE, lcd::WHITE, 4);
    y += 60;
    snprintf(buf, sizeof(buf), "Panel %dx%d RGB565, PCLK %lu MHz", LCD_W, LCD_H,
             (unsigned long)(LCD_PCLK_HZ / 1000000));
    lcd::text(30, y, buf, lcd::CYAN, lcd::CYAN, 2);
    y += 30;
    snprintf(buf, sizeof(buf), "PSRAM free %u / %u KB", (unsigned)(ESP.getFreePsram() / 1024),
             (unsigned)(ESP.getPsramSize() / 1024));
    lcd::text(30, y, buf, lcd::CYAN, lcd::CYAN, 2);
    y += 30;
    if (g_touchOk) {
        snprintf(buf, sizeof(buf), "Touch GT%s @0x%02X fw 0x%04X cfg v%u %ux%u", g_tpInfo.productId,
                 g_tpInfo.addr, g_tpInfo.firmware, g_tpInfo.cfgVersion, g_tpInfo.cfgX,
                 g_tpInfo.cfgY);
        lcd::text(30, y, buf, lcd::GREEN, lcd::GREEN, 2);
    } else {
        lcd::text(30, y, "Touch: GT911 NOT FOUND", lcd::RED, lcd::RED, 2);
    }
    y += 60;
    lcd::text(30, y, "Tap screen to start touch test", lcd::YELLOW, lcd::YELLOW, 3);
}

struct Pattern {
    void (*draw)();
};

const Pattern PATTERNS[] = {
    {[] { patternSolid(lcd::RED, "Red"); }},
    {[] { patternSolid(lcd::GREEN, "Green"); }},
    {[] { patternSolid(lcd::BLUE, "Blue"); }},
    {[] { patternSolid(lcd::WHITE, "White"); }},
    {[] { patternSolid(lcd::BLACK, "Black"); }},
    {patternColorBars},
    {patternGradients},
    {patternBitWalk},
    {patternGeometry},
    {patternInfo},
};
constexpr int PATTERN_COUNT = sizeof(PATTERNS) / sizeof(PATTERNS[0]);

// ---------------------------------------------------------------------------
// Touch screen

void drawButton(const Button& b, uint16_t bg) {
    lcd::fillRect(b.x, b.y, b.w, b.h, bg);
    lcd::rect(b.x, b.y, b.w, b.h, lcd::WHITE);
    int tw = lcd::textWidth(b.label, 2);
    lcd::text(b.x + (b.w - tw) / 2, b.y + (b.h - 16) / 2, b.label, lcd::WHITE, bg, 2);
}

void drawTarget(int x, int y) {
    lcd::hline(x - 15, y, 31, lcd::GRAY);
    lcd::vline(x, y - 15, 31, lcd::GRAY);
    lcd::rect(x - 5, y - 5, 11, 11, lcd::GRAY);
    char buf[16];
    snprintf(buf, sizeof(buf), "%d,%d", x, y);
    int tx = x < LCD_W / 2 ? x + 20 : x - 20 - lcd::textWidth(buf, 1);
    lcd::text(tx, y + 8, buf, lcd::GRAY, lcd::GRAY, 1);
}

void drawTouchHeader(bool force) {
    char status[64], coords[96];
    snprintf(status, sizeof(status), "TOUCH  n=%d  %lu rd/s  err %lu", g_lastCount,
             (unsigned long)g_readsPerSecond, (unsigned long)g_touchErrors);
    coords[0] = '\0';
    for (int i = 0; i < g_lastCount; i++) {
        size_t len = strlen(coords);
        snprintf(coords + len, sizeof(coords) - len, "%u:%u,%u;", g_pts[i].id, g_pts[i].x,
                 g_pts[i].y);
    }
    char key[sizeof(g_lastHeaderText)];
    snprintf(key, sizeof(key), "%s|%s", status, coords);
    if (!force && strcmp(key, g_lastHeaderText) == 0) return;
    strcpy(g_lastHeaderText, key);

    if (g_hdrBuf) lcd::setTarget(g_hdrBuf, LCD_W, HEADER_H);
    lcd::fillRect(0, 0, LCD_W, HEADER_H, lcd::DARKGRAY);
    lcd::text(8, 8, status, lcd::WHITE, lcd::DARKGRAY, 2);
    int x = 8;
    char buf[24];
    for (int i = 0; i < g_lastCount; i++) {
        snprintf(buf, sizeof(buf), "%u:%u,%u", g_pts[i].id, g_pts[i].x, g_pts[i].y);
        uint16_t c = TRAIL_COLORS[g_pts[i].id % 8];
        lcd::text(x, 38, buf, c, lcd::DARKGRAY, 2);
        x += lcd::textWidth(buf, 2) + 14;
    }
    drawButton(BTN_CLEAR, lcd::rgb(120, 30, 30));
    drawButton(BTN_PATTERNS, lcd::rgb(30, 60, 120));
    if (g_hdrBuf) {
        lcd::setTarget(nullptr, 0, 0);
        lcd::blit(0, 0, LCD_W, HEADER_H, g_hdrBuf);
    }
    g_lastHeaderAt = millis();
    g_headerDirty = false;
}

void drawTouchCanvas() {
    lcd::fillRect(0, HEADER_H, LCD_W, LCD_H - HEADER_H, lcd::BLACK);
    lcd::hline(0, HEADER_H, LCD_W, lcd::GRAY);
    drawTarget(30, HEADER_H + 30);
    drawTarget(LCD_W - 31, HEADER_H + 30);
    drawTarget(30, LCD_H - 31);
    drawTarget(LCD_W - 31, LCD_H - 31);
    drawTarget(LCD_W / 2, (LCD_H + HEADER_H) / 2);
    lcd::rect(0, 0, LCD_W, LCD_H, lcd::GRAY);
    if (!g_touchOk) {
        lcd::text(200, 250, "GT911 NOT DETECTED", lcd::RED, lcd::RED, 4);
    }
}

void enterTouchMode() {
    g_mode = Mode::Touch;
    for (int i = 0; i < 16; i++) g_lastX[i] = -1;
    drawTouchCanvas();
    drawTouchHeader(true);
    Serial.println("Mode: touch test");
}

void enterPatternMode() {
    g_mode = Mode::Patterns;
    g_pattern = 0;
    PATTERNS[0].draw();
    g_patternShownAt = millis();
    Serial.println("Mode: patterns");
}

void nextPattern() {
    g_pattern++;
    if (g_pattern >= PATTERN_COUNT) {
        enterTouchMode();
        return;
    }
    Serial.printf("Pattern %d/%d\n", g_pattern + 1, PATTERN_COUNT);
    PATTERNS[g_pattern].draw();
    g_patternShownAt = millis();
}

// Returns true on a new touch-down (0 -> N points). Updates g_pts / g_lastCount.
bool pollTouch(bool* changed) {
    *changed = false;
    if (!g_touchOk) return false;
    gt911::Point pts[gt911::MAX_POINTS];
    bool ready = false;
    int n = gt911::read(pts, &ready);
    g_readsInSecond++;
    if (n < 0) {
        g_touchErrors++;
        return false;
    }
    uint32_t now = millis();
    if (!ready) {
        // Fallback in case the controller does not report an empty frame on release.
        if (g_lastCount > 0 && now - g_lastFrameAt > TOUCH_RELEASE_TIMEOUT_MS) {
            Serial.printf("Touch up by timeout (last %u,%u)\n", g_pts[0].x, g_pts[0].y);
            g_lastCount = 0;
            *changed = true;
        }
        return false;
    }
    g_lastFrameAt = now;
    bool down = (g_lastCount == 0 && n > 0);
    *changed = (n > 0 || g_lastCount > 0);

    for (int i = 0; i < n; i++) {
        if (pts[i].x >= LCD_W) pts[i].x = LCD_W - 1;
        if (pts[i].y >= LCD_H) pts[i].y = LCD_H - 1;
    }
    if (down) Serial.printf("Touch down: id %u at %u,%u\n", pts[0].id, pts[0].x, pts[0].y);
    if (n == 0 && g_lastCount > 0) {
        Serial.printf("Touch up (last %u,%u)\n", g_pts[0].x, g_pts[0].y);
    }
    memcpy(g_pts, pts, sizeof(pts));
    g_lastCount = n;
    return down;
}

void handleTouchMode(bool down, bool changed) {
    if (down) {
        const gt911::Point& p = g_pts[0];
        if (BTN_CLEAR.hit(p.x, p.y)) {
            Serial.println("Button CLEAR");
            drawTouchCanvas();
            drawTouchHeader(true);
            return;
        }
        if (BTN_PATTERNS.hit(p.x, p.y)) {
            Serial.println("Button PATTERNS");
            enterPatternMode();
            return;
        }
        for (int i = 0; i < 16; i++) g_lastX[i] = -1;
    }
    if (!changed) return;

    for (int i = 0; i < g_lastCount; i++) {
        const gt911::Point& p = g_pts[i];
        if (p.y < HEADER_H) continue;
        uint8_t id = p.id & 0x0F;
        uint16_t c = TRAIL_COLORS[id % 8];
        if (g_lastX[id] >= 0) {
            lcd::line(g_lastX[id], g_lastY[id], p.x, p.y, 4, c);
        } else {
            lcd::circle(p.x, p.y, 4, c);
        }
        g_lastX[id] = p.x;
        g_lastY[id] = p.y;
    }
    if (g_lastCount == 0) {
        for (int i = 0; i < 16; i++) g_lastX[i] = -1;
    }
    g_headerDirty = true;
}

void startDiTest(uint32_t intervalMs) {
    g_diTest = true;
    g_diIntervalMs = intervalMs;
    g_diReads = g_diErrors = g_diMaxUs = g_diSumUs = 0;
    g_diLast = -1;
    g_diLastReport = millis();
    g_tpErrorsAtDiStart = g_touchErrors;
    ch422g::setPin(EXIO_LCD_BL, false);
    Serial.printf("DI test: read every %lu ms, backlight OFF. Watch for flashes. "
                  "Any key or touch ends the test.\n", (unsigned long)intervalMs);
    Serial.println("DI lines are active LOW (optocoupler pulls EXIO to GND when input is powered)");
}

void stopDiTest(const char* reason) {
    g_diTest = false;
    ch422g::setPin(EXIO_LCD_BL, g_backlight);
    Serial.printf("DI test end (%s): %lu reads, %lu errors, input window avg %lu us / max %lu us, "
                  "touch I2C errors during test %lu\n",
                  reason, (unsigned long)g_diReads, (unsigned long)g_diErrors,
                  (unsigned long)(g_diReads ? g_diSumUs / g_diReads : 0), (unsigned long)g_diMaxUs,
                  (unsigned long)(g_touchErrors - g_tpErrorsAtDiStart));
}

void handleDiTest(uint32_t now) {
    if (now - g_diLastRead < g_diIntervalMs) return;
    g_diLastRead = now;
    uint8_t in = 0;
    uint32_t us = 0;
    if (!ch422g::readInputs(&in, &us)) {
        g_diErrors++;
        return;
    }
    g_diReads++;
    g_diSumUs += us;
    if (us > g_diMaxUs) g_diMaxUs = us;
    int di = ((in >> EXIO_DI0) & 1) | (((in >> EXIO_DI1) & 1) << 1);
    if (di != g_diLast) {
        Serial.printf("DI0=%d (%s)  DI1=%d (%s)  raw 0x%02X\n", di & 1, (di & 1) ? "idle" : "ACTIVE",
                      (di >> 1) & 1, ((di >> 1) & 1) ? "idle" : "ACTIVE", in);
        g_diLast = di;
    }
    if (now - g_diLastReport >= 5000) {
        g_diLastReport = now;
        Serial.printf("  %lu reads, avg %lu us, max %lu us\n", (unsigned long)g_diReads,
                      (unsigned long)(g_diSumUs / g_diReads), (unsigned long)g_diMaxUs);
    }
}

void handleSerial() {
    while (Serial.available()) {
        char c = Serial.read();
        if (g_diTest) {
            stopDiTest("key");
            continue;
        }
        switch (c) {
            case 'n':
                if (g_mode == Mode::Patterns) nextPattern();
                break;
            case 'p':
                enterPatternMode();
                break;
            case 't':
                enterTouchMode();
                break;
            case 'b':
                g_backlight = !g_backlight;
                ch422g::setPin(EXIO_LCD_BL, g_backlight);
                Serial.printf("Backlight %s\n", g_backlight ? "ON" : "OFF");
                break;
            case 's':
                i2cScan();
                break;
            case 'd':
                startDiTest(200);
                break;
            case 'f':
                startDiTest(20);
                break;
            default:
                break;
        }
    }
}

}  // namespace

void setup() {
    Serial.begin(115200);
    uint32_t t0 = millis();
    while (!Serial && millis() - t0 < 2000) delay(10);
    Serial.println("\n=== ESP32-S3-Touch-LCD-4.3B display/touch test ===");
    Serial.printf("CPU %lu MHz, flash %lu MB, PSRAM %lu KB (free %lu KB)\n",
                  (unsigned long)getCpuFrequencyMhz(),
                  (unsigned long)(ESP.getFlashChipSize() / (1024 * 1024)),
                  (unsigned long)(ESP.getPsramSize() / 1024),
                  (unsigned long)(ESP.getFreePsram() / 1024));
    if (ESP.getPsramSize() == 0) Serial.println("ERROR: no PSRAM - framebuffer cannot be allocated");

    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, I2C_FREQ_HZ);
    i2cScan();

    if (ch422g::begin()) {
        Serial.printf("CH422G OK, outputs 0x%02X (backlight on)\n", ch422g::state());
    } else {
        Serial.println("ERROR: CH422G not responding - backlight stays off");
    }

    if (lcd::begin()) {
        Serial.printf("LCD OK, framebuffer %p\n", lcd::framebuffer());
    } else {
        Serial.println("ERROR: LCD init failed");
        while (true) delay(1000);
    }

    g_touchOk = gt911::begin(&g_tpInfo);
    if (g_touchOk) {
        Serial.printf("GT911 OK: addr 0x%02X, product '%s', fw 0x%04X, cfg v%u, res %ux%u\n",
                      g_tpInfo.addr, g_tpInfo.productId, g_tpInfo.firmware, g_tpInfo.cfgVersion,
                      g_tpInfo.cfgX, g_tpInfo.cfgY);
    } else {
        Serial.println("ERROR: GT911 not found at 0x5D/0x14");
    }

    g_hdrBuf = static_cast<uint16_t*>(heap_caps_malloc(LCD_W * HEADER_H * sizeof(uint16_t),
                                                       MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    if (!g_hdrBuf) {
        g_hdrBuf = static_cast<uint16_t*>(
            heap_caps_malloc(LCD_W * HEADER_H * sizeof(uint16_t), MALLOC_CAP_SPIRAM));
    }
    if (!g_hdrBuf) Serial.println("WARN: no memory for header buffer, drawing directly");

    Serial.println("Commands: n=next p=patterns t=touch b=backlight s=i2c scan d/f=DI test 200/20 ms");
    enterPatternMode();
}

void loop() {
    handleSerial();

    uint32_t now = millis();
    if (now - g_rateWindowStart >= 1000) {
        g_readsPerSecond = g_readsInSecond;
        g_readsInSecond = 0;
        g_rateWindowStart = now;
    }

    bool changed = false, down = false;
    if (now - g_lastPoll >= TOUCH_POLL_MS) {
        g_lastPoll = now;
        down = pollTouch(&changed);
    }

    if (g_diTest) {
        // Touch ends the test (and proves the touch controller survived the IO direction flips).
        if (down) {
            stopDiTest("touch");
            Serial.printf("Touch still OK at %u,%u\n", g_pts[0].x, g_pts[0].y);
        } else {
            handleDiTest(now);
        }
        return;
    }

    if (g_mode == Mode::Patterns) {
        // The info screen waits for a tap, unless touch is dead (then it times out too).
        bool waitForTap = g_touchOk && g_pattern == PATTERN_COUNT - 1;
        if (down || (!waitForTap && now - g_patternShownAt >= PATTERN_TIMEOUT_MS)) nextPattern();
    } else {
        handleTouchMode(down, changed);
        // Header refresh is rate-limited; also refreshes the rd/s counter once per second.
        if (g_mode == Mode::Touch && now - g_lastHeaderAt >= HEADER_MIN_INTERVAL_MS &&
            (g_headerDirty || now - g_lastHeaderAt >= 1000)) {
            drawTouchHeader(false);
        }
    }
}
