#include <Arduino.h>
#include <FastLED.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Wire.h>

TaskHandle_t displayTaskHandle;
void displayTask(void* parameter);


// === Function Prototypes ===
void readMSGEQ7();
void updateDynamicMaxBand();
void cyclePalettes();
void printBandsForPlotter();
void updateDynamicMaxTreble();
void updatePaletteBlend();
void updateBassBrightnessOverlay();
void initializeScreen();
void updateDisplay();
void handleButton();
void handlePotentiometer();
void resetToDefaults();
void handleStrobe();
void handleFlash();
void handleRedSegmentBounce();


// === Constants and Globals ===  

// screen
#define SCREEN_I2C_ADDR 0x3C // or 0x3C
#define SCREEN_WIDTH 128     // OLED display width, in pixels
#define SCREEN_HEIGHT 64     // OLED display height, in pixels
#define OLED_RST_PIN -1      // Reset pin (-1 if not available)
// Create display object
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

#define POT_PIN 32
#define BUTTON_PIN 15
#define EFFECT_PIN 27  // or any free capacitive pin
#define FLASH_PIN 14
#define EFFECT_PIN2 34
int touchThreshold = 40;  // Adjust depending on your setup
int touchThreshold2 = 40;

int initialPotRaw = -1;
bool potLocked = true;
const int potDeadzone = 20;  // Adjust as needed
bool showResetMessage = false;
unsigned long lastResetMessageTime = 0;

bool strobeActive = false;
unsigned long lastStrobeToggle = 0;
bool strobeOn = false;
const uint16_t strobeInterval = 60;  // ms between flashes
uint8_t strobeEnvelope = 0;           // 0–maxBassBrightness
bool strobeFadingIn = false;
bool strobeFadingOut = false;
bool flashTriggered = false;
unsigned long flashStartTime = 0;

bool inRedSegmentMode = false;
bool redSegmentTrigger = false;
bool bounceSegmentActive = false;
unsigned long lastRedSegmentTime = 0;
int redSegmentPos = 0;
int redSegmentDir = 1;
const int redSegmentLength = 5;
const unsigned long redSegmentDuration = 300;
unsigned long flashHoldStart = 0;
unsigned long strobeFadeStart = 0;


// msgeq7
#define STROBE_PIN 12
#define RESET_PIN 13
#define ANALOG_PIN 36

// LED strips
#define DATA_PIN_1 25
#define DATA_PIN_2 26
#define NUM_LEDS 50
#define CHIPSET WS2811
#define COLOR_ORDER RGB
#define BRIGHTNESS 170

// music variables

int selectedBand1 = 1; // LED strip 1 (bass)
int selectedBand2 = 5; // LED strip 2 (bass)
const int paletteCycleDuration = 22000;
float maxBandValue2 = 650;  // Upper clamp for bass band value
const unsigned long trebleCooldown = 60;  // minimum gap between treble triggers (ms)
const uint8_t trebleHitIntensity = 255;     // more subtle brightness
const unsigned long tuningTimeout = 5000;  // 5 seconds without input = exit tuning mode

//adjust with potentiometer
int noiseFloor = 45;
int maxBassBrightness = 160;   // Higher maximum for bass hits
int maxTrebleBrightness = 220; // Higher maximum for treble hits
int baseBrightness = 50; // very dim 
float bassThresholdFactor = 0.45; //percentage of max band value that's considered within threshold
float trebleThresholdFactor = 0.3; //
int bassDeltaThreshold = 100;  // min gap between bass triggers (ms)
int trebleDecayMS = 150; //every n milliseconds, max treble reader is decaying x / 1023 
int trebleDecayRaw = 1; // x / 1023
int bassDecayMS = 150; //every n milliseconds, max treble reader is decaying x / 1023 
int bassDecayRaw = 1; // x / 1023
int bassSegmentJump = 2;
int trebleSegmentJump = 2;
unsigned long bassFadeDuration = 730;  // Fade back to base in 2.5 sec
#define TREBLE_HISTORY 75  // number of past readings to average



enum SettingType { SET_INT, SET_FLOAT };

struct TunableSetting {
  const char* name;
  void* ptr;
  SettingType type;
  float minVal;
  float maxVal;
  float defaultVal;
};


#define NUM_SETTINGS 12

TunableSetting settings[NUM_SETTINGS] = {
  { "Noise Floor",           &noiseFloor,           SET_INT,   0,   800,  80 },  // min, max, default
  { "Max Bass Brightness",   &maxBassBrightness,    SET_INT,   20,  255,  180 },
  { "Max Treble Brightness", &maxTrebleBrightness,  SET_INT,   20,  255,  220 },
  { "Base Brightness",       &baseBrightness,       SET_INT,   5,   150,  55 },
  { "Bass Threshold Factor", &bassThresholdFactor,  SET_FLOAT, 0.01, 1.0, 0.45 },
  { "Treble Threshold Fact.",&trebleThresholdFactor,SET_FLOAT, 0.01, 1.0, 0.4 },
  { "Bass Delta",            &bassDeltaThreshold,   SET_INT,   10,  300,  90 },
  { "Treble Decay ms",       &trebleDecayMS,        SET_INT,   10,  1000, 100 },
  { "Treble Decay Raw",      &trebleDecayRaw,       SET_INT,   1,   15,   1 },
  { "Bass Decay ms",         &bassDecayMS,          SET_INT,   10,  1000, 100 },
  { "Bass Decay Raw",        &bassDecayRaw,         SET_INT,   1,   15,   1 },
  { "Bass Segment Jump",     &bassSegmentJump,      SET_INT,   1,   8,    1 }
};


// don't change
volatile bool isTuning = false;
unsigned long lastTuningChange = 0;
int currentSettingIndex = 0;
unsigned long lastDebounceTime = 0;
unsigned long debounceDelay = 250;
bool lastButtonState = HIGH;
int maxTrebleValue = 0;  // variable
unsigned long lastTrebleTrigger = 0;
unsigned long lastBassHit = 0;
CRGB trebleBursts[NUM_LEDS];     // stores active burst overlays
const uint8_t trebleFadeSpeed = 2;  // lower = slower fade, try 1–3
int maxBandValue = 0; // max bass
int trebleBurstPos = 0;      // Current position of treble burst center
int trebleBurstDir = 1;      // +1 = right, -1 = left (bouncing direction)
unsigned long bassFadeStart = 0;
static int bassLitStart = 0;
static int bassLitLength = 10;
static int bassMoveDir = 1; // 1 = right, -1 = left
CRGB bassSparkles[NUM_LEDS];  // stores red flicker state
uint8_t sparkleFlicker[NUM_LEDS] = {0};
uint8_t bassBrightnessOverlay[NUM_LEDS] = {0};
uint8_t currentBassBrightness = baseBrightness;
uint8_t targetBassBrightness = baseBrightness;
int trebleOverlayPos = 0;           // Start position for the segment
int trebleSegmentDirection = 1; // -1 for backward
float trebleHistory[TREBLE_HISTORY] = {0};
int trebleIndex = 0;
float trebleThreshold = 0; 
float lastSmoothBand1 = 0;
float threshold1 = 0;
float smoothBands[7];
CRGB leds1[NUM_LEDS];
CRGB leds2[NUM_LEDS];
uint8_t colorScheme1 = 0;
uint8_t colorScheme2 = 1;


DEFINE_GRADIENT_PALETTE(indigoPurpleBluePalette) {
  0, 75, 0, 130,
  64, 138, 43, 226,
  128, 0, 0, 255,
  192, 255, 0, 255,
  255, 75, 0, 130
};

// NEW: Teal / Blue / Green / Pink replacement for RedGold
DEFINE_GRADIENT_PALETTE(tealBlueGreenPinkPalette) {
  0,   0,  128, 100,    // Deep teal
  64,  0,  180, 130,    // Aqua
  128, 0,  220, 180,    // Seafoam
  192, 40, 255, 200,    // Soft mint
  255, 0,  140, 255     // Light lavender blue (cool finish)
};

// Slightly tuned Green / Indigo / Orange
DEFINE_GRADIENT_PALETTE(greenIndigoOrangePalette) {
  0, 75, 0, 130,
  64, 138, 43, 226,
  128, 0, 0, 255,
  192, 255, 0, 255,
  255, 75, 0, 130,
   0,   0,  128, 100,    // Deep teal
  64,  0,  180, 130,    // Aqua
  128, 0,  220, 180,    // Seafoam
};

DEFINE_GRADIENT_PALETTE(intenseRedPaletteWithIndigo) {
   160, 128, 0, 0,       // Darker red
  160, 128, 0, 0,       // Darker red
   160, 128, 0, 0,       // Darker red
  160, 128, 0, 0      // Darker red
};



CRGB deepRedPalette[] = {
  CRGB(90, 0, 0),
  CRGB(120, 0, 0),
  CRGB(200, 0, 0),
  CRGB(255, 30, 30)
};

CRGBPalette16 sharedPalette = indigoPurpleBluePalette;
CRGBPalette16 targetSharedPalette = indigoPurpleBluePalette;
CRGBPalette16 treblePalette = intenseRedPaletteWithIndigo;
CRGBPalette16 previousPalette = sharedPalette;
unsigned long paletteBlendStart = 0;
bool isBlending = false;


// === Setup ===
void setup() {
  Serial.begin(115200);
  pinMode(STROBE_PIN, OUTPUT);
  pinMode(RESET_PIN, OUTPUT);
  pinMode(ANALOG_PIN, INPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(EFFECT_PIN, INPUT_PULLUP);
  pinMode(FLASH_PIN, INPUT_PULLUP);
  pinMode(EFFECT_PIN2, INPUT_PULLUP);

  FastLED.addLeds<CHIPSET, DATA_PIN_1, COLOR_ORDER>(leds1, NUM_LEDS);
  FastLED.addLeds<CHIPSET, DATA_PIN_2, COLOR_ORDER>(leds2, NUM_LEDS);
  FastLED.setBrightness(BRIGHTNESS);

  digitalWrite(RESET_PIN, LOW);
  digitalWrite(STROBE_PIN, LOW);
  delay(1);
  digitalWrite(RESET_PIN, HIGH);
  for (int i = 0; i < 7; i++) smoothBands[i] = 0;
  
  initializeScreen();
  resetToDefaults(); 
  updateDisplay(); // show Home Screen initially


  xTaskCreatePinnedToCore(
  displayTask,           // Task function
  "Display Task",        // Task name
  4096,                  // Stack size
  NULL,                  // Task parameters
  1,                     // Priority (low)
  &displayTaskHandle,    // Task handle
  0                      // Run on Core 0
);
}

// === Main Loop ===
void loop() {
  unsigned long now = millis();
  
    // === Update touch state ===
  handleButton();  // sets strobeActive, flashTriggered, etc.
  handleRedSegmentBounce();
// === Handle effects that override music ===
handleStrobe();  // Always run, handles active + fading
handleFlash();   // One-shot hit

// Block music mode if strobe is still visible
if (strobeActive || strobeFadingOut || strobeEnvelope > 0) {
  return;
}
  
  readMSGEQ7();
  updateDynamicMaxBand();
  updateDynamicMaxTreble();
  printBandsForPlotter();
  updatePaletteBlend();
  updateBassBrightnessOverlay();
  //updateDisplay();
  handlePotentiometer();
  
  static unsigned long lastCycle = 0;
  if (now - lastCycle > 10000) {  // every 30 seconds
  cyclePalettes();
  lastCycle = now;
}

  // Store previous smoothed value
float currentSmoothBand1 = smoothBands[selectedBand1];

// Compute bass delta trigger
bool bassDeltaTrigger = (currentSmoothBand1 - lastSmoothBand1) > bassDeltaThreshold;

if (bassDeltaTrigger) {
    lastBassHit = now;
}
lastSmoothBand1 = currentSmoothBand1;

   float bassValue = smoothBands[selectedBand1];
  float trebleValue = smoothBands[selectedBand2];

if (bassValue > threshold1) {
  uint8_t newBrightness;

 if (bassValue > maxBandValue * 0.9) {
  newBrightness = maxBassBrightness;
  bassLitLength = 15;
} else if (bassValue > maxBandValue * 0.75) {
  newBrightness = maxBassBrightness * 0.94;
  bassLitLength = 12;
} else if (bassValue > maxBandValue * 0.65) {
  newBrightness = maxBassBrightness * 0.89;
  bassLitLength = 10;
} else if (bassValue > maxBandValue * 0.55) {
  newBrightness = maxBassBrightness * 0.81;
  bassLitLength = 8;
} else if (bassValue > maxBandValue * 0.45) {
  newBrightness = maxBassBrightness * 0.75;
  bassLitLength = 6;
} else if (bassValue > maxBandValue * 0.35) {
  newBrightness = maxBassBrightness * 0.66;
  bassLitLength = 4;
} else if (bassValue > maxBandValue * 0.25) {
  newBrightness = maxBassBrightness * 0.58;
  bassLitLength = 3;
} else if (bassValue > maxBandValue * 0.15) {
  newBrightness = maxBassBrightness * 0.5;
  bassLitLength = 2;
} else {
  newBrightness = maxBassBrightness * 0.4;
  bassLitLength = 2;
}

  newBrightness = constrain(newBrightness, baseBrightness, maxBassBrightness);

  if (newBrightness > currentBassBrightness) {
  currentBassBrightness = newBrightness;
  targetBassBrightness = newBrightness;
  bassFadeStart = now;

  // Determine segment length based on intensity
  //float intensity = (bassValue - threshold1) / (maxBandValue - threshold1);
  //intensity = constrain(intensity, 0.0, 1.0);
  //bassLitLength = map(intensity * 100, 0, 100, 2, 14);

  // Move lit segment in current direction
  bassLitStart += bassMoveDir * bassSegmentJump;

  // Bounce at ends
  if (bassLitStart <= 0) {
    bassLitStart = 0;
    bassMoveDir = 1;
  } else if (bassLitStart + bassLitLength >= NUM_LEDS) {
    bassLitStart = NUM_LEDS - bassLitLength;
    bassMoveDir = -1;
  }
}
}


// Compute fade back to baseBrightness
unsigned long fadeElapsed = now - bassFadeStart;
if (fadeElapsed < bassFadeDuration) {
  float fadeProgress = (float)fadeElapsed / bassFadeDuration;
  currentBassBrightness = targetBassBrightness - (targetBassBrightness - baseBrightness) * fadeProgress;
} else {
  currentBassBrightness = baseBrightness;
}

bool showBassGlitter = (bassValue > threshold1);  // Only on valid hits
float intensityRatio = constrain((bassValue - threshold1) / (maxBandValue - threshold1), 0.0, 1.0);


// Sparkle chance and brightness scale with intensity
uint8_t sparkleChance = map(intensityRatio * 100, 0, 100, 0, 20);
uint8_t sparkleStrength = map(intensityRatio * 100, 0, 100, 60, 255);

// Add new sparkles (intensity-scaled)
if (showBassGlitter) {
  for (int i = bassLitStart; i < bassLitStart + bassLitLength; i++) {
    if (i >= 0 && i < NUM_LEDS && random8() < sparkleChance) {
      bassSparkles[i] += CRGB::Red;  // or CRGB(sparkleStrength, 0, 0) for intensity scaling
    }
  }
}

// Fade old sparkles
for (int i = 0; i < NUM_LEDS; i++) {
  bassSparkles[i].fadeToBlackBy(25);  // adjust for longer or shorter persistence
}

  for (int i = 0; i < NUM_LEDS; i++) {
  uint8_t index = map(i, 0, NUM_LEDS - 1, 0, 255);
  uint8_t brightness = bassBrightnessOverlay[i];

  // Base palette color using precomputed overlay
  leds1[i] = ColorFromPalette(sharedPalette, index, brightness);

  // Flicker: sparkle decay & red flash
  if (sparkleFlicker[i] > 0) {
    sparkleFlicker[i] = qsub8(sparkleFlicker[i], 6);  // decay flicker
    uint8_t flickerIndex = map(sparkleFlicker[i], 0, 255, 0, 3);
    leds1[i] += deepRedPalette[flickerIndex];
  }

  // Blend in red sparkles on top of palette
  leds1[i] = blend(leds1[i], bassSparkles[i], 235);
}


  // Base brightness on treble strip
  for (int i = 0; i < NUM_LEDS; i++) {
    uint8_t index = map(i, 0, NUM_LEDS - 1, 0, 255);
    leds2[i] = ColorFromPalette(sharedPalette, index, (baseBrightness * 2));
  }

  // Treble burst trigger with back-and-forth movement
  if (trebleValue > trebleThreshold && millis() - lastTrebleTrigger > trebleCooldown) {
  lastTrebleTrigger = millis();

  // Calculate intensity ratio
  float trebleIntensity = (trebleValue - trebleThreshold) / (maxTrebleValue - trebleThreshold);
  trebleIntensity = constrain(trebleIntensity, 0.0, 1.0);

  // Determine brightness and segment size
  uint8_t burstBrightness;
  int burstSize;

  if (trebleIntensity > 0.9) {
    burstBrightness = maxTrebleBrightness;
    burstSize = 5;
  } else if (trebleIntensity > 0.75) {
    burstBrightness = maxTrebleBrightness * 0.92;
    burstSize = 4;
  } else if (trebleIntensity > 0.6) {
    burstBrightness = maxTrebleBrightness * 0.87;
    burstSize = 3;
    } else if (trebleIntensity > 0.5) {
    burstBrightness = maxTrebleBrightness * 0.82;
    burstSize = 2;
  } else if (trebleIntensity > 0.4) {
    burstBrightness = maxTrebleBrightness * 0.72;
    burstSize = 1;
  } else {
    burstBrightness = maxTrebleBrightness * .6;
    burstSize = 1;
  }

  // Bounce center position
  int center = trebleBurstPos;
  trebleBurstPos += trebleBurstDir * trebleSegmentJump;

  if (trebleBurstPos >= NUM_LEDS - 2 || trebleBurstPos <= 2) {
    trebleBurstDir *= -1;
    trebleBurstPos = constrain(trebleBurstPos, 2, NUM_LEDS - 3);
  }

  // Light up burst segment around center
  for (int i = center - burstSize; i <= center + burstSize; i++) {
    if (i >= 0 && i < NUM_LEDS) {
      uint8_t index = map(i, 0, NUM_LEDS - 1, 0, 255);
      trebleBursts[i] += ColorFromPalette(treblePalette, index, burstBrightness);
    }
  }
}



  for (int i = 0; i < NUM_LEDS; i++) {
    trebleBursts[i].fadeToBlackBy(10);
    leds2[i] = blend(leds2[i], trebleBursts[i], 225);
  }

  FastLED.show();
  delay(5);
}


// Read MSGEQ7 values and smooth them
void readMSGEQ7() {
  analogRead(ANALOG_PIN);
  delayMicroseconds(5);
  digitalWrite(RESET_PIN, HIGH);
  delayMicroseconds(1);
  digitalWrite(RESET_PIN, LOW);

  for (int i = 0; i < 7; i++) {
    digitalWrite(STROBE_PIN, LOW);  
    delayMicroseconds(60);  
    
    int rawValue = analogRead(ANALOG_PIN); // 0–4095
    digitalWrite(STROBE_PIN, HIGH); 
    delayMicroseconds(30);

    int scaledValue = map(rawValue, 0, 4095, 0, 900);

    const int MAX_REASONABLE_RAW = 700;
    const int MAX_JUMP = 350;
    float previous = smoothBands[i];

    if (scaledValue > MAX_REASONABLE_RAW || abs(scaledValue - previous) > MAX_JUMP) {
      scaledValue = previous;  // Discard unreasonable spike
    }

    // Exponential moving average
    smoothBands[i] = scaledValue * 0.9 + previous * 0.1;

    if (i == selectedBand2) {
      trebleHistory[trebleIndex] = smoothBands[i];
      trebleIndex = (trebleIndex + 1) % TREBLE_HISTORY;
    }

    if (smoothBands[i] < noiseFloor) {
      smoothBands[i] = 0;
    }
  }
}


// Clean serial output for Python plotter
void printBandsForPlotter() {
  for (int i = 0; i < 7; i++) {
    Serial.print(smoothBands[i], 2);
    Serial.print(",");  // Always add a comma
  }
  Serial.print(threshold1, 2);         // Band 7
  Serial.print(",");                  
  Serial.println(trebleThreshold, 2);  // Band 8
}

// Cycle through predefined palettes
void cyclePalettes() {
  colorScheme1 = (colorScheme1 + 1) % 3;

  switch (colorScheme1) {
    case 0: targetSharedPalette = indigoPurpleBluePalette; break;
    case 1: targetSharedPalette = tealBlueGreenPinkPalette; break;
    case 2: targetSharedPalette = greenIndigoOrangePalette; break;
  }

  previousPalette = sharedPalette;   // Capture current palette before changing
  paletteBlendStart = millis();      // Record time of transition
  isBlending = true;                 // Flag to activate timed blending
}

void updateDynamicMaxBand() {
  static unsigned long lastBandUpdate = 0;
  unsigned long now = millis();
  float current = smoothBands[selectedBand1];

  if (current > maxBandValue) {
    maxBandValue = min(current, maxBandValue2);  // Optional clamp
  }

  if (now - lastBandUpdate > bassDecayMS) {
    lastBandUpdate = now;
    maxBandValue -= bassDecayRaw;  // Decay step
    if (maxBandValue < 10) maxBandValue = 10;  // floor clamp
  }

  threshold1 = maxBandValue * bassThresholdFactor;  // Or whatever % works well
}

float computeAdaptiveTrebleThreshold(float* history, int length) {
  float sorted[length];
  memcpy(sorted, history, sizeof(float) * length);

  // Simple bubble sort
  for (int i = 0; i < length - 1; i++) {
    for (int j = 0; j < length - i - 1; j++) {
      if (sorted[j] > sorted[j + 1]) {
        float temp = sorted[j];
        sorted[j] = sorted[j + 1];
        sorted[j + 1] = temp;
      }
    }
  }

  int startIdx = length * 0.05;  // Skip bottom 5%
  float sum = 0;
  for (int i = startIdx; i < length; i++) {
    sum += sorted[i];
  }

  return sum / (length - startIdx);
}

void updateDynamicMaxTreble() {
  static unsigned long lastTrebleUpdate = 0;
  unsigned long now = millis();
  float currentTreble = smoothBands[selectedBand2];

  // Update peak tracking
  if (currentTreble > maxTrebleValue) {
    maxTrebleValue = min(currentTreble, maxBandValue2);
  }

  // Decay over time
  if (now - lastTrebleUpdate > trebleDecayMS) {
    lastTrebleUpdate = now;
    maxTrebleValue -= trebleDecayRaw;
    maxTrebleValue = constrain(maxTrebleValue, 10, maxBandValue2);
  }

  // Compute smoothed trimmed average
  float adaptiveAvg = computeAdaptiveTrebleThreshold(trebleHistory, TREBLE_HISTORY);

  // Conservative floor: use average slightly boosted
  float adaptiveFloor = adaptiveAvg * 1.05;  // smaller boost

  // Peak-driven threshold for sharp responsiveness
  float peakFloor = maxTrebleValue * trebleThresholdFactor;

  // === Smooth blending instead of hard max ===
  trebleThreshold = (adaptiveFloor * 0.6) + (peakFloor * 0.4);

  // Optional: avoid overly low threshold on silence
  if (trebleThreshold < 30) trebleThreshold = 30;
}

CRGBPalette16 blendedPalette;
uint8_t paletteBlendAmount = 0;

void updatePaletteBlend() {
  if (isBlending) {
    unsigned long elapsed = millis() - paletteBlendStart;

    if (elapsed >= paletteCycleDuration) {
      sharedPalette = targetSharedPalette;
      isBlending = false;
    } else {
      paletteBlendAmount = map(elapsed, 0, paletteCycleDuration, 0, 255);
      nblendPaletteTowardPalette(sharedPalette, targetSharedPalette, 40);  // palette blend speed
    }
  }
}

void updateBassBrightnessOverlay() {
  for (int i = 0; i < NUM_LEDS; i++) {
    bassBrightnessOverlay[i] = baseBrightness;
  }
  for (int i = bassLitStart; i < bassLitStart + bassLitLength; i++) {
    if (i >= 0 && i < NUM_LEDS) {
      bassBrightnessOverlay[i] = currentBassBrightness;
    }
  }
}

void initializeScreen() {
  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_I2C_ADDR)) {
    Serial.println(F("SSD1306 init failed"));
    while (true);
  }

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println(F("MSGEQ7 Visualizer"));
  display.println(F("Initializing..."));
  display.display();
  delay(1000);
}

void updateDisplay() {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  if (isTuning) {
    TunableSetting& s = settings[currentSettingIndex];
    display.println("TUNING:");
    display.println(s.name);
    display.print("Val: ");
    if (s.type == SET_INT)
      display.println(*((int*)s.ptr));
    else
      display.println(*((float*)s.ptr), 2);
  } else {
    display.println("MSGEQ7 Visualizer");
    display.setCursor(0, 20);
    display.println("Visualizer Running...");
    // Optional: show palette name or mode here
  }

  display.display();
}


void displayTask(void* parameter) {
  bool lastTuningState = isTuning;

  while (true) {
    unsigned long now = millis();

    if (showResetMessage && now - lastResetMessageTime > 2000) {
      showResetMessage = false;
      updateDisplay();  // return to normal after 2 sec
    }

    if (isTuning && now - lastTuningChange > tuningTimeout) {
      isTuning = false;
      lastTuningState = false;
      updateDisplay();
    } else if (isTuning != lastTuningState) {
      updateDisplay();
      lastTuningState = isTuning;
    }

    vTaskDelay(100 / portTICK_PERIOD_MS);
  }
}


void handleButton() {
  unsigned long now = millis();

  // === FLASH BUTTON ===
bool currentFlashTouch = touchRead(FLASH_PIN) < touchThreshold;
static bool lastFlashTouch = false;
static bool flashHoldHandled = false;

if (currentFlashTouch && !lastFlashTouch) {
  flashHoldStart = now;
  flashHoldHandled = false;
}

if (currentFlashTouch && !flashHoldHandled) {
  if (now - flashHoldStart >= 1500) {
    inRedSegmentMode = !inRedSegmentMode;
    Serial.print("Toggled Red Segment Mode: ");
    Serial.println(inRedSegmentMode);
    flashHoldHandled = true;  // Only once per hold
  }
}

if (!currentFlashTouch && lastFlashTouch) {
  if (!flashHoldHandled) {
    // It's a tap
    if (inRedSegmentMode) {
      redSegmentTrigger = true;
    } else {
      flashTriggered = true;
      flashStartTime = now;  // Important: reset start time
    }
  }
}


lastFlashTouch = currentFlashTouch;


  // === STROBE BUTTON ===
  static bool lastStrobeTouch = false;
  bool currentStrobeTouch = touchRead(EFFECT_PIN) < touchThreshold;

  strobeActive = currentStrobeTouch;  // active while held
  if (!currentStrobeTouch && lastStrobeTouch) {
    strobeFadingOut = true;
    strobeFadeStart = now;
  }
  lastStrobeTouch = currentStrobeTouch;

  // === RED SEGMENT DIRECT BUTTON (if still using EFFECT_PIN2) ===
  static bool lastEffectTouch2 = false;
  bool currentEffectTouch2 = touchRead(EFFECT_PIN2) < touchThreshold;

  if (currentEffectTouch2 && !lastEffectTouch2) {
    redSegmentTrigger = true;
  }

  lastEffectTouch2 = currentEffectTouch2;

  // === SETTINGS BUTTON (BUTTON_PIN) ===
  static unsigned long pressStart = 0;
  static bool wasPressed = false;
  static bool longPressTriggered = false;

  bool pressed = touchRead(BUTTON_PIN) < touchThreshold;

  if (pressed && !wasPressed) {
    pressStart = now;
    longPressTriggered = false;
  }

  if (pressed && !longPressTriggered && (now - pressStart > 1500)) {
    resetToDefaults();  // long press
    longPressTriggered = true;
  }

  if (!pressed && wasPressed && !longPressTriggered) {
    // short press = next setting
    currentSettingIndex = (currentSettingIndex + 1) % NUM_SETTINGS;
    isTuning = true;
    lastTuningChange = now;

    // Remap pot
    TunableSetting& s = settings[currentSettingIndex];
    if (s.type == SET_INT) {
      initialPotRaw = map((int)s.defaultVal, (int)s.minVal, (int)s.maxVal, 0, 4095);
    } else {
      float ratio = (s.defaultVal - s.minVal) / (s.maxVal - s.minVal);
      initialPotRaw = ratio * 4095;
    }
    potLocked = true;
    updateDisplay();
  }

  wasPressed = pressed;
}


void handlePotentiometer() {
  if (!isTuning) return;

  analogRead(POT_PIN);  // dummy read for ADC settling
  delayMicroseconds(5);
  int raw = analogRead(POT_PIN);

  if (potLocked) {
  if (abs(raw - initialPotRaw) <= potDeadzone) {
    // Pot is within target range (default value)
    potLocked = false;
    Serial.println("Pot unlocked – now tracking changes.");
  } else {
    return; // Still locked
  }
}


  TunableSetting& setting = settings[currentSettingIndex];

  int mapped = map(raw, 0, 4095, (int)setting.minVal, (int)setting.maxVal);

  static int lastValue = -9999;
  int threshold = ((int)setting.maxVal - (int)setting.minVal < 10) ? 0 : 1;

  if (abs(mapped - lastValue) >= threshold) {
    if (setting.type == SET_INT) {
      *((int*)setting.ptr) = mapped;
    } else {
      float mappedFloat = map(raw, 0, 4095, 0, 1000) / 1000.0;
      *((float*)setting.ptr) = mappedFloat * (setting.maxVal - setting.minVal) + setting.minVal;
    }

    lastValue = mapped;
    updateDisplay();  // Only update screen if it changed
  }

  // Always extend tuning timeout when pot is touched
  lastTuningChange = millis();
}



void resetToDefaults() {
  for (int i = 0; i < NUM_SETTINGS; i++) {
    if (settings[i].type == SET_INT)
      *((int*)settings[i].ptr) = (int)settings[i].defaultVal;
    else
      *((float*)settings[i].ptr) = settings[i].defaultVal;
  }

  Serial.println("Settings reset to default.");
  isTuning = true;
  lastTuningChange = millis();

  // Show "Defaults Restored" non-blocking
  display.clearDisplay();
  display.setCursor(0, 30);
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.println("Defaults Restored");
  display.display();

  // Use a flag to auto-restore screen later
  lastResetMessageTime = millis();
  showResetMessage = true;
}
void handleStrobe() {
  static unsigned long lastToggle = 0;
  static bool strobeOn = false;

  // Start fading out when released
  if (!strobeActive && strobeEnvelope > 0) {
    strobeFadingOut = true;
  }

  // Fade out envelope
  if (strobeFadingOut) {
    if (strobeEnvelope > 0) {
      strobeEnvelope -= 6;
    } else {
      strobeEnvelope = 0;
      strobeFadingOut = false;
    }
  }

  // Fade in when button held
  if (strobeActive && strobeEnvelope < maxBassBrightness) {
    strobeEnvelope += 6;
  }

  // Flash toggle logic
  if (millis() - lastToggle > strobeInterval) {
    strobeOn = !strobeOn;
    lastToggle = millis();
  }

  CRGB color1 = CRGB(strobeEnvelope, strobeEnvelope, strobeEnvelope);  // Soft white
  CRGB color2 = CRGB(0, 0, strobeEnvelope);                             // Blue

  for (int i = 0; i < NUM_LEDS; i++) {
    leds1[i] = strobeOn ? color1 : CRGB::Black;
    leds2[i] = strobeOn ? color2 : CRGB::Black;
  }

  FastLED.show();
}


void handleFlash() {
  if (!flashTriggered) return;

  unsigned long elapsed = millis() - flashStartTime;
  if (elapsed >= bassFadeDuration) {
    flashTriggered = false;
    return;
  }

  float fadeProgress = 1.0 - (float)elapsed / bassFadeDuration;
  uint8_t flashBrightness = fadeProgress * maxBassBrightness;

  for (int i = 0; i < NUM_LEDS; i++) {
    uint8_t index = map(i, 0, NUM_LEDS - 1, 0, 255);
    leds1[i] = ColorFromPalette(sharedPalette, index, flashBrightness);
    leds2[i] = ColorFromPalette(sharedPalette, 255 - index, flashBrightness);
  }

  FastLED.show();
}

void handleRedSegmentBounce() {
  unsigned long now = millis();

  if (redSegmentTrigger) {
    redSegmentTrigger = false;
    bounceSegmentActive = true;
    lastRedSegmentTime = now;

    redSegmentPos += redSegmentDir;
    if (redSegmentPos <= 0) {
      redSegmentPos = 0;
      redSegmentDir = 1;
    } else if (redSegmentPos >= NUM_LEDS - redSegmentLength) {
      redSegmentPos = NUM_LEDS - redSegmentLength;
      redSegmentDir = -1;
    }
  }

  if (bounceSegmentActive) {
    // Clear and draw red segment
    fadeToBlackBy(leds1, NUM_LEDS, 2);  // Smooth fade
    for (int i = 0; i < redSegmentLength; i++) {
      int index = redSegmentPos + i;
      if (index >= 0 && index < NUM_LEDS) {
        leds1[index] = CRGB::Red;
      }
    }

    // Auto-clear after timeout
    if (now - lastRedSegmentTime > redSegmentDuration) {
      bounceSegmentActive = false;
      fadeToBlackBy(leds1, NUM_LEDS, 240);  // Fully fade out
    }

    FastLED.show();
  }
}
