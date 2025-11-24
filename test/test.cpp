#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ---------- OLED ----------
static const int I2C_SDA = 21;
static const int I2C_SCL = 22;
#define OLED_WIDTH   128
#define OLED_HEIGHT   64
static const uint8_t TRY_ADDRS[] = {0x3C, 0x3D};
Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, -1);

// ---------- MSGEQ7 (your same pins) ----------
#define STROBE_PIN 12
#define RESET_PIN  13
#define ANALOG_PIN 36  // ESP32 ADC1_CH0  (good choice)

// ---------- Options ----------
#define SERIAL_DEBUG true

// 0..900 scale (matches your big project)
static float smooth900[7] = {0,0,0,0,0,0,0};
static const float EMA = 0.35f;  // smoothing strength

// ===== util: I2C scan (optional, but handy) =====
uint8_t scanI2C() {
  uint8_t found = 0;
  Serial.println("\nI2C scan...");
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.printf("  Found device at 0x%02X\n", addr);
      if (!found) found = addr;
    }
    delay(2);
  }
  if (!found) Serial.println("  No I2C devices found.");
  return found;
}

// ===== OLED helpers =====
bool beginOLED() {
  uint8_t first = scanI2C();
  bool ok = false; uint8_t used = 0;

  if (first) {
    ok = display.begin(SSD1306_SWITCHCAPVCC, first);
    used = first;
  }
  for (uint8_t a : TRY_ADDRS) {
    if (!ok && a != first) {
      ok = display.begin(SSD1306_SWITCHCAPVCC, a);
      if (ok) used = a;
    }
  }
  if (ok) {
    Serial.printf("SSD1306 init OK at 0x%02X\n", used);
  }
  return ok;
}

void drawBanner(const char* sub) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(2);
  display.setCursor(0,0);
  display.println("MSGEQ7");
  display.setTextSize(1);
  display.setCursor(0,22);
  display.println(sub);
  display.display();
}

// ===== MSGEQ7 read =====
// Returns values in out[7] on a 0..900 scale.
void readMSGEQ7_900(int out[7]) {
  // Flush ADC channel switch noise (ESP32 quirk)
  (void)analogRead(ANALOG_PIN);
  delayMicroseconds(5);
  (void)analogRead(ANALOG_PIN);
  delayMicroseconds(5);

  // Reset sequence per datasheet
  digitalWrite(STROBE_PIN, HIGH);
  digitalWrite(RESET_PIN,  HIGH); delayMicroseconds(5);
  digitalWrite(RESET_PIN,  LOW);  delayMicroseconds(5);

  for (int i = 0; i < 7; i++) {
    digitalWrite(STROBE_PIN, LOW);
    delayMicroseconds(30);          // allow filter to settle

    (void)analogRead(ANALOG_PIN);   // throw away first read after mux
    int raw = analogRead(ANALOG_PIN);

    digitalWrite(STROBE_PIN, HIGH); // latch next band
    delayMicroseconds(36);

    // Map ESP32 12-bit ADC (0..4095) to your traditional 0..900 scale
    int v900 = map(raw, 0, 4095, 0, 900);
    out[i] = constrain(v900, 0, 900);
  }
}

// ===== Render a 7-band bar graph =====
void drawBands(const int v[7]) {
  // Layout: 7 columns across width, labels at bottom
  const int left   = 0;
  const int top    = 0;
  const int width  = OLED_WIDTH;
  const int height = OLED_HEIGHT;

  // graph area (leave 10px for text at bottom)
  const int graphH = height - 12;
  const int graphW = width;
  const int colW   = graphW / 7;
  const int gap    = 2;

  display.clearDisplay();

  // Title
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0,0);
  display.print("MSGEQ7  (0..900)");

  // Bars
  for (int i = 0; i < 7; i++) {
    int x = left + i * colW + gap/2;
    int w = colW - gap;
    w = max(w, 2);

    // Map 0..900 to 0..graphH-14
    int h = map(v[i], 0, 900, 0, graphH - 4);
    int y = top + graphH - h;

    // frame
    display.drawRect(x, top + 10, w, graphH - 10, SSD1306_WHITE);

    // fill
    if (h > 0) {
      display.fillRect(x+1, y, w-2, h, SSD1306_WHITE);
    }

    // tiny label value under each column
    display.setCursor(x, height - 10);
    char buf[6];
    snprintf(buf, sizeof(buf), "%d", v[i]);
    display.print(buf);
  }

  display.display();
}

void setup() {
  Serial.begin(115200);
  delay(200);

  // I2C / OLED
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(400000);
  if (!beginOLED()) {
    Serial.println("SSD1306 init FAILED. Check wiring / address.");
    while (true) delay(100);
  }
  drawBanner("Waiting for audio...");

  // ADC config (ESP32)
  analogReadResolution(12);
  analogSetPinAttenuation(ANALOG_PIN, ADC_11db); // up to ~3.3–3.6V

  // MSGEQ7 pins
  pinMode(STROBE_PIN, OUTPUT);
  pinMode(RESET_PIN,  OUTPUT);
  pinMode(ANALOG_PIN, INPUT);

  digitalWrite(STROBE_PIN, HIGH);
  digitalWrite(RESET_PIN,  LOW);

  delay(200);
}

void loop() {
  int bands[7];
  readMSGEQ7_900(bands);

  // smooth for a nicer display
  for (int i = 0; i < 7; i++) {
    smooth900[i] = smooth900[i] + EMA * (bands[i] - smooth900[i]);
    bands[i] = (int)(smooth900[i] + 0.5f);
  }

  // draw to OLED
  drawBands(bands);

  // optional serial
  if (SERIAL_DEBUG) {
    Serial.printf("Bands: ");
    for (int i = 0; i < 7; i++) {
      Serial.printf("%d%s", bands[i], (i<6?", ":"\n"));
    }
  }

  delay(20); // ~50 FPS
}
