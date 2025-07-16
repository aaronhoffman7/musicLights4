#include <Arduino.h>
#include <FastLED.h>

// === Function Prototypes ===
void readMSGEQ7();
void handleSerialInput();
void updateDynamicMaxBand();
void bounceEffect(CRGB* leds, CRGBPalette16 palette, int trailLength, int pos, uint8_t hueShift);
void rainbowEffect(CRGB* leds, int speed);
void strobeEffect();
void flashEffect();
void vibesEffect(CRGB* leds, CRGBPalette16 palette);
void updateBassLeds(CRGB* targetLeds, float bandValue, float threshold, CRGBPalette16 palette, uint8_t* timers);
void updateTrebleLeds(CRGB* targetLeds, float bandValue, float threshold, CRGBPalette16 palette, uint8_t* timers);
void cyclePalettes();
void printBandsForPlotter();
void readTouchInputs();
void updateDynamicMaxTreble();
CRGBPalette16 blendPalettes(const CRGBPalette16& from, const CRGBPalette16& to, uint8_t amount);


// === Constants and Globals ===  
#define STROBE_PIN 12
#define RESET_PIN 13
#define ANALOG_PIN 36

#define DATA_PIN_1 25
#define DATA_PIN_2 26
#define NUM_LEDS 50
#define CHIPSET WS2811
#define COLOR_ORDER RGB
#define BRIGHTNESS 170

#define STROBE_DURATION 1300       // milliseconds
#define STROBE_FLASH_SPEED 45      // milliseconds
#define FLASH_DURATION 300         // milliseconds

//buttons
bool currentButtonState = HIGH;
bool lastButtonState = HIGH;
unsigned long lastButtonPollTime = 0;
const unsigned long buttonPollInterval = 15;  // ~15ms button poll interval
const int buttonPin = 32;

//music variables
const int noiseFloor = 45;
int maxTrebleValue = 0;  // variable
int maxTrebleValue2 = 650; //start value (decays from here)
unsigned long lastTrebleTrigger = 0;
unsigned long lastBassHit = 0;
CRGB trebleColorDefault = CRGB(255, 0, 0);  // bright red for most palettes
CRGB trebleColorAlt = CRGB(255, 0, 0);    // teal-blue for alternate vibe
const int paletteCycleDuration = 10000;
const int trebleBand = 4;
int selectedBand1 = 1; // LED strip 1 (bass)
int selectedBand2 = 4; // LED strip 2 (bass)

uint8_t blendProgress = 0;
unsigned long lastBlendTime = 0;
bool blending = false;

const int maxBassBrightness = 130;   // Higher maximum for bass hits
const int maxTrebleBrightness = 220; // Higher maximum for treble hits
const int baseBrightness = 11; // very dim 
const int trebleDensity = 4; // treble shows on every nth light on the strip 
const int trebleBlend = 200; // n/255 ratio of how much treble overly blends with bass base
const uint8_t trebleFadeSpeed = 2;  // lower = slower fade, try 1–3
const unsigned long trebleCooldown = 60;  // minimum gap between treble triggers (ms)
const uint8_t trebleHitIntensity = 255;     // more subtle brightness
const float bassThresholdFactor = 0.3; //percentage of max band value that's considered within threshold
const float trebleThresholdFactor = 0.32; //
const unsigned long bassTrebleGap = 1;  // adjust gap between bass/treble flashes
const int bassDeltaThreshold = 50;  // min gap between bass triggers (ms)
const int trebleDecayMS = 150; //every n milliseconds, max treble reader is decaying x / 1023 
const int trebleDecayRaw = 1; // x / 1023
const int bassDecayMS = 150; //every n milliseconds, max treble reader is decaying x / 1023 
const int bassDecayRaw = 3; // x / 1023
int maxBandValue = 0; // max bass
float maxBandValue2 = 800;  // Upper clamp for bass band value
int burstLength = 4;            // number of LEDs to light up (1 on either side of center)
CRGB trebleBursts[NUM_LEDS];     // stores active burst overlays
int trebleBurstPos = 0;      // Current position of treble burst center
int trebleBurstDir = 1;      // +1 = right, -1 = left (bouncing direction)
const int bassSegmentJump = 2;


uint8_t currentBassBrightness = baseBrightness;
uint8_t targetBassBrightness = baseBrightness;
unsigned long bassFadeStart = 0;
const unsigned long bassFadeDuration = 550;  // Fade back to base in 2.5 sec
static int bassLitStart = 0;
static int bassLitLength = 10;
static int bassMoveDir = 1; // 1 = right, -1 = left

CRGB bassSparkles[NUM_LEDS];  // stores red flicker state



const int paletteBlendSpeed = 1;
int trebleOverlayPos = 0;           // Start position for the segment
const int segmentLength = 6;  // Number of LEDs in the segment
int trebleSegmentDirection = 1; // -1 for backward
#define TREBLE_HISTORY 75  // number of past readings to average
float trebleHistory[TREBLE_HISTORY] = {0};
int trebleIndex = 0;

float trebleThreshold = 0; 
float lastSmoothBand1 = 0;
float threshold1 = 0;

float smoothBands[7];
int currentPosition = 0;


bool lightsOn = true;     // global on/off
bool buttonPressed = false;
bool strobeState = false;

unsigned long lastStrobeFlash = 0;
unsigned long strobeStartTime = 0;
bool flashActive = false;
unsigned long flashStartTime = 0;
const int minLEDsLit = 1;

CRGB leds1[NUM_LEDS];
CRGB leds2[NUM_LEDS];
CRGB targetLeds1[NUM_LEDS];
CRGB targetLeds2[NUM_LEDS];
CRGB trebleOverlay1[NUM_LEDS];
CRGB trebleOverlay2[NUM_LEDS];
uint8_t trebleOverlayFade = 0;


uint8_t lingerTimers1[NUM_LEDS] = {0};
uint8_t lingerTimers2[NUM_LEDS] = {0};
const uint8_t lingerDuration = 100;  // frames to linger (~20 x 5ms = 100ms)

String effectModes[] = {"off", "music", "treble", "vibes", "rainbow", "bounce"};
const int numModes = sizeof(effectModes) / sizeof(effectModes[0]);
int currentModeIndex = 0;
String currentMode = effectModes[currentModeIndex];


int bouncePosition = 0;
int bounceDirection = 1;
uint8_t bounceHueShift = 0;

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
  0, 0, 255, 150,     // Teal
  64, 0, 200, 255,    // Light blue
  128, 0, 255, 0,     // Greenish
  192, 255, 0, 150,   // Pinkish highlight
  255, 0, 255, 255    // Soft lavender
};

// Slightly tuned Green / Indigo / Orange
DEFINE_GRADIENT_PALETTE(greenIndigoOrangePalette) {
  0,   0,   100,  50,     // Deep green-teal
  64,  0,   180, 100,     // Rich turquoise
  100, 50,  0,   150,     // Violet-indigo
  128, 100, 0,   180,     // Deep indigo
  160, 180, 30,  100,     // Sunset gold
  192, 255, 100,  0,      // Warm orange
  224, 255, 180,  0,      // Bright amber
  255, 255, 140,  40      // Golden pop
};


DEFINE_GRADIENT_PALETTE(intenseRedPaletteWithIndigo) {
  192, 75,  0, 130,     // Indigo
  224, 60,  0, 100,     // Deeper indigo
  0,   255, 0, 0,       // Pure red
  96,  255, 16, 16,     // Hot red
  128, 200, 0, 0,       // Deep crimson
  160, 128, 0, 0,       // Darker red
  192, 75,  0, 130,     // Indigo
  224, 60,  0, 100,     // Deeper indigo
  255, 30,  0, 60       // Fading indigo
};



CRGBPalette16 sharedPalette = indigoPurpleBluePalette;  // Main shared palette
CRGBPalette16 targetSharedPalette = indigoPurpleBluePalette;
CRGBPalette16 treblePalette = intenseRedPaletteWithIndigo; // Separate for treble
CRGBPalette16 previousPalette = sharedPalette;  // store the palette we're blending from
unsigned long paletteBlendStart = 0;
bool isBlending = false;



// === Setup ===
void setup() {
  Serial.begin(115200);
  pinMode(buttonPin, INPUT);
  pinMode(STROBE_PIN, OUTPUT);
  pinMode(RESET_PIN, OUTPUT);
  pinMode(ANALOG_PIN, INPUT);

  FastLED.addLeds<CHIPSET, DATA_PIN_1, COLOR_ORDER>(leds1, NUM_LEDS);
  FastLED.addLeds<CHIPSET, DATA_PIN_2, COLOR_ORDER>(leds2, NUM_LEDS);
  FastLED.setBrightness(BRIGHTNESS);

  digitalWrite(RESET_PIN, LOW);
  digitalWrite(STROBE_PIN, LOW);
  delay(1);
  digitalWrite(RESET_PIN, HIGH);

  currentModeIndex = 0;
  currentMode = effectModes[currentModeIndex];  // should be "music"
  for (int i = 0; i < 7; i++) smoothBands[i] = 0;
}


// === Main Loop ===
void loop() {
  handleSerialInput();
  readMSGEQ7();
  updateDynamicMaxBand();
  updateDynamicMaxTreble();
  printBandsForPlotter();

  unsigned long now = millis();  // <== ONLY declare this once here

static unsigned long lastPaletteBlendTime = 0;
unsigned long paletteBlendInterval = 39; // Try 100–300 ms for slower blending

// === Time-based palette blending ===
if (isBlending) {
  unsigned long elapsed = now - paletteBlendStart;

  if (elapsed >= paletteCycleDuration) {
    sharedPalette = targetSharedPalette;
    isBlending = false;
  } else {
    uint8_t blendAmount = map(elapsed, 0, paletteCycleDuration, 0, 255);
    sharedPalette = blendPalettes(previousPalette, targetSharedPalette, blendAmount);
  }
}


  // Store previous smoothed value
float currentSmoothBand1 = smoothBands[selectedBand1];

// Compute bass delta trigger
bool bassDeltaTrigger = (currentSmoothBand1 - lastSmoothBand1) > bassDeltaThreshold;

if (bassDeltaTrigger) {
    lastBassHit = now;
}
lastSmoothBand1 = currentSmoothBand1;


  if (now - lastButtonPollTime > buttonPollInterval) {
      lastButtonPollTime = now;

      currentButtonState = digitalRead(buttonPin);

      if (currentButtonState == LOW && lastButtonState == HIGH) {
          currentModeIndex = (currentModeIndex + 1) % numModes;
          currentMode = effectModes[currentModeIndex];
          Serial.print("Mode changed to: "); Serial.println(currentMode);
      }
      lastButtonState = currentButtonState;
  }

  if (currentMode != "off") {
    static unsigned long lastPaletteChange = 0;
    if (now - lastPaletteChange > paletteCycleDuration) {
        cyclePalettes();
        lastPaletteChange = now;
    }
}



      if (currentMode == "bounce") {
          bouncePosition += bounceDirection;
          if (bouncePosition >= NUM_LEDS || bouncePosition < 0) {
              bounceDirection *= -1;
              bouncePosition += bounceDirection;
          }
          bounceHueShift++;
          bounceEffect(leds1, sharedPalette, 7, bouncePosition, bounceHueShift);
          bounceEffect(leds2, sharedPalette, 10, bouncePosition, bounceHueShift);
          FastLED.show();
          delay(30);
      } 
      else if (currentMode == "rainbow") {
          rainbowEffect(leds1, 20);
          rainbowEffect(leds2, 20);
          FastLED.show();
      }
      else if (currentMode == "strobe") {
          strobeEffect();
      }
      else if (currentMode == "flash") {
          flashEffect();
      }

  if (currentMode == "music") {
   float bassValue = smoothBands[selectedBand1];
  float trebleValue = smoothBands[selectedBand2];
  static int bassStartIndex = 0;
static int bassLitLength = 0;


  // Bass brightness mapping with baseBrightness fallback
  uint8_t bassBrightness = baseBrightness;

if (bassValue > threshold1) {
  uint8_t newBrightness;

  // Quantized brightness tiers
  if (bassValue > maxBandValue * 0.95) {
    newBrightness = maxBassBrightness; // full hit
  } else if (bassValue > maxBandValue * 0.85) {
    newBrightness = maxBassBrightness * 0.85;
  } else if (bassValue > maxBandValue * 0.70) {
    newBrightness = maxBassBrightness * 0.65;
  } else if (bassValue > maxBandValue * 0.55) {
    newBrightness = maxBassBrightness * 0.50;
  } else {
    newBrightness = baseBrightness; // just fade-up slightly on weak hits
  }

  newBrightness = constrain(newBrightness, baseBrightness, maxBassBrightness);

  if (newBrightness > currentBassBrightness) {
  currentBassBrightness = newBrightness;
  targetBassBrightness = newBrightness;
  bassFadeStart = now;

  // Determine segment length based on intensity
  float intensity = (bassValue - threshold1) / (maxBandValue - threshold1);
  intensity = constrain(intensity, 0.0, 1.0);
  bassLitLength = map(intensity * 100, 0, 100, 6, 14);

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

//what the strip do

bool showBassGlitter = (bassValue > threshold1);  // Only on valid hits
float intensityRatio = constrain((bassValue - threshold1) / (maxBandValue - threshold1), 0.0, 1.0);


// Sparkle chance and brightness scale with intensity
uint8_t sparkleChance = map(intensityRatio * 100, 0, 100, 0, 30);
uint8_t sparkleStrength = map(intensityRatio * 100, 0, 100, 60, 255);

// Fade old sparkles
for (int i = 0; i < NUM_LEDS; i++) {
  bassSparkles[i].fadeToBlackBy(25);  // adjust for longer or shorter persistence
}

// Add new sparkles (intensity-scaled)
if (showBassGlitter) {
  for (int i = bassLitStart; i < bassLitStart + bassLitLength; i++) {
    if (i >= 0 && i < NUM_LEDS && random8() < sparkleChance) {
      bassSparkles[i] += CRGB::Red;  // or CRGB(sparkleStrength, 0, 0) for intensity scaling
    }
  }
}


  for (int i = 0; i < NUM_LEDS; i++) {
  uint8_t index = map(i, 0, NUM_LEDS - 1, 0, 255);
  bool inMain = (i >= bassLitStart && i < bassLitStart + bassLitLength);
  bool isEdge = (i == bassLitStart - 1 || i == bassLitStart + bassLitLength);

  uint8_t brightness = baseBrightness;
  if (inMain) {
    brightness = currentBassBrightness;
  } else if (isEdge) {
    brightness = currentBassBrightness / 2;
  }

  // Base palette color
  leds1[i] = ColorFromPalette(sharedPalette, index, brightness);

  // Blend in persistent red sparkle effect
  leds1[i] = blend(leds1[i], bassSparkles[i], 128);  // 50% blend strength
}




  // Base brightness on treble strip
  for (int i = 0; i < NUM_LEDS; i++) {
    uint8_t index = map(i, 0, NUM_LEDS - 1, 0, 255);
    leds2[i] = ColorFromPalette(sharedPalette, index, (baseBrightness * 2));
  }

  // Treble burst trigger with back-and-forth movement
  if (trebleValue > trebleThreshold && millis() - lastTrebleTrigger > trebleCooldown) {
    lastTrebleTrigger = millis();

    int center = trebleBurstPos;
    trebleBurstPos += trebleBurstDir * 3;

    if (trebleBurstPos >= NUM_LEDS - 2 || trebleBurstPos <= 2) {
      trebleBurstDir *= -1;
      trebleBurstPos = constrain(trebleBurstPos, 2, NUM_LEDS - 3);
    }

    for (int i = center - 2; i <= center + 2; i++) {
      if (i >= 0 && i < NUM_LEDS) {
        uint8_t index = map(i, 0, NUM_LEDS - 1, 0, 255);
        trebleBursts[i] += ColorFromPalette(treblePalette, index, maxTrebleBrightness);
      }
    }
  }


  for (int i = 0; i < NUM_LEDS; i++) {
    trebleBursts[i].fadeToBlackBy(10);
    leds2[i] = blend(leds2[i], trebleBursts[i], 150);
  }

  FastLED.show();
  delay(5);
}

else if (currentMode == "treble") {
    // Fade overlay
    trebleOverlayFade = qsub8(trebleOverlayFade, trebleFadeSpeed);

    // Clear LEDs by default (no bass visuals)
   for (int i = 0; i < NUM_LEDS; i++) {
  uint8_t paletteIndex = map(i, 0, NUM_LEDS - 1, 0, 255);
  leds1[i] = ColorFromPalette(sharedPalette, paletteIndex, baseBrightness);
  leds2[i] = ColorFromPalette(sharedPalette, paletteIndex, baseBrightness);
}

    // Treble detection and cooldown
    bool trebleIsStrong = smoothBands[trebleBand] > trebleThreshold;
    bool trebleCooldownElapsed = (now - lastTrebleTrigger > trebleCooldown);

if (trebleIsStrong && trebleCooldownElapsed) {
    trebleOverlayFade = trebleHitIntensity;
    lastTrebleTrigger = now;

    // Update position with bouncing behavior
    trebleOverlayPos += trebleSegmentDirection * (segmentLength + 1);

    if (trebleOverlayPos + segmentLength >= NUM_LEDS) {
        trebleOverlayPos = NUM_LEDS- segmentLength;
        trebleSegmentDirection = -1;  // Bounce back
    } else if (trebleOverlayPos <= 0) {
        trebleOverlayPos = 0;
        trebleSegmentDirection = 1;   // Bounce forward
    }
}


// Draw treble segment with fade
if (trebleOverlayFade > 0) {
  for (int i = 0; i < segmentLength; i++) {
    int ledIndex = trebleOverlayPos + i;
    if (ledIndex >= 0 && ledIndex < NUM_LEDS) {
      CRGB overlayColor = ColorFromPalette(sharedPalette, i * (255 / segmentLength), trebleOverlayFade);
      leds1[ledIndex] = blend(leds1[ledIndex], overlayColor, trebleBlend);
      leds2[ledIndex] = blend(leds2[ledIndex], overlayColor, trebleBlend);  // optional
    }
  }
}
    FastLED.show();
    delay(5);
}

    else if (currentMode == "vibes") {
          vibesEffect(leds1, sharedPalette);
          vibesEffect(leds2, sharedPalette);
          FastLED.show();
      }
      
      else if (currentMode == "off") {
    FastLED.clear();
    FastLED.show();
      }

}


// Read MSGEQ7 values and smooth them
void readMSGEQ7() {
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
    const int MAX_JUMP = 150;
    float previous = smoothBands[i];

    if (scaledValue > MAX_REASONABLE_RAW || abs(scaledValue - previous) > MAX_JUMP) {
      scaledValue = previous;  // Discard unreasonable spike
    }

    // Exponential moving average
    smoothBands[i] = scaledValue * 0.9 + previous * 0.1;

    if (i == trebleBand) {
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


void updateTrebleLeds(CRGB* targetLeds, float trebleValue, float threshold, CRGBPalette16 palette, uint8_t* timers) {
  if (trebleValue >= threshold) {
    uint8_t trebleHitBrightness = map(trebleValue, threshold, maxTrebleValue, 100, maxTrebleBrightness);
    trebleHitBrightness = constrain(trebleHitBrightness, 100, maxTrebleBrightness);

    for (int i = 0; i < NUM_LEDS; i += trebleDensity) {
      uint8_t paletteIndex = map(i, 0, NUM_LEDS - 1, 0, 255);
      targetLeds[i] = ColorFromPalette(sharedPalette, paletteIndex, trebleHitBrightness);
      timers[i] = lingerDuration;
    }
  }

  for (int i = 0; i < NUM_LEDS; i++) {
    if (timers[i] > 0) {
      timers[i]--;
      targetLeds[i].nscale8(trebleFadeSpeed);  // <-- now matches bass style
    } else {
      targetLeds[i] = CRGB::Black;
    }
  }
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



void handleSerialInput() {
  if (Serial.available() > 0) {
    String input = Serial.readStringUntil('\n'); // Read input until newline
    input.trim(); // Remove extra spaces or line breaks

    if (input == "bounce") {
      currentMode = "bounce";
      Serial.println("Mode set to BOUNCE");
    } else if (input == "rainbow") {
      currentMode = "rainbow";
      Serial.println("Mode set to RAINBOW");
    } else if (input == "s") {
      currentMode = "strobe";
      strobeStartTime = millis();
      Serial.println("Strobe effect activated!");
    } else if (input == "F" || input == "f") {
      currentMode = "flash";
      Serial.println("Flash effect activated!");
    } else if (input == "vibes") {
      currentMode = "vibes";
      Serial.println("Vibes effect activated!");
    } else if (input == "treble") {
      currentMode = "treble";
      Serial.println("Treble mode activated!");
    } else if (input == "music") {
      currentMode = "music";
      Serial.println("Music mode activated!");
    } else if (input == "off") {
      currentMode = "off";
      Serial.println("Lights off");
    } else {
      Serial.println("Unknown command. Available: bounce, rainbow, vibes, music, treble, off, s, F");
    }
  }
}

void bounceEffect(CRGB* leds, CRGBPalette16 sharedPalette, int trailLength, int pos, uint8_t hueShift) {
  fadeToBlackBy(leds, NUM_LEDS, 30);

  for (int i = 0; i < trailLength; i++) {
    int trailPos = pos - i * bounceDirection;
    if (trailPos >= 0 && trailPos < NUM_LEDS) {
      uint8_t brightness = BRIGHTNESS / (i + 1);
      uint8_t colorIndex = (hueShift + i * 10) % 255;
      leds[trailPos] = ColorFromPalette(sharedPalette, colorIndex, 255);
    }
  }
}

void rainbowEffect(CRGB* leds, int speed) {
  static uint8_t hue = 0; // Starting hue
  fill_rainbow(leds, NUM_LEDS, hue, 7); // Fill LEDs with a rainbow
  hue++; // Increment hue for animation
  FastLED.show();
  delay(speed);
}

void strobeEffect() {
  unsigned long currentTime = millis();

  if (currentTime - strobeStartTime > STROBE_DURATION) {
    currentMode = "music";  
    return;
  }

  if (currentTime - lastStrobeFlash > STROBE_FLASH_SPEED) {
    strobeState = !strobeState;
    lastStrobeFlash = currentTime;
  }

  if (strobeState) {
    fill_solid(leds1, NUM_LEDS, CRGB::Blue);
    fill_solid(leds2, NUM_LEDS, CRGB::White);
  } else {
    fill_solid(leds1, NUM_LEDS, CRGB::Black);
    fill_solid(leds2, NUM_LEDS, CRGB::Black);
  }

  FastLED.show();
}

void flashEffect(){

  unsigned long currentTime = millis();

  if (!flashActive) {
    flashActive = true;
    flashStartTime = currentTime;
  }

  if (currentTime - flashStartTime < FLASH_DURATION) {
    fill_solid(leds1, NUM_LEDS, CRGB::Red);   // Bright burst color
    fill_solid(leds2, NUM_LEDS, CRGB::Indigo);
    FastLED.show();
  } else {
    flashActive = false;
    currentMode = "music";  // Return to default mode after flash
  }
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
  float currentTreble = smoothBands[trebleBand];

  if (currentTreble > maxTrebleValue) {
    maxTrebleValue = min(currentTreble, maxBandValue2);
  }

  if (now - lastTrebleUpdate > trebleDecayMS) {
    lastTrebleUpdate = now;
    maxTrebleValue -= trebleDecayRaw;
    if (maxTrebleValue < 10) maxTrebleValue = 10;
  }

  // Compute adaptive threshold from treble history
  trebleThreshold = computeAdaptiveTrebleThreshold(trebleHistory, TREBLE_HISTORY) * 1.8;
}

void vibesEffect(CRGB* leds, CRGBPalette16 sharedPalette) {
  static uint8_t hueShift = 0;
  static uint16_t sparkleTimer = 0;

  // Slowly fade existing LEDs to create trails
  for (int i = 0; i < NUM_LEDS; i++) {
    leds[i].fadeToBlackBy(8);  // Soft trailing effect
  }

  // Occasionally add a sparkle at a random position
  if (random8() < 50) {  // Adjust probability for density
    int pos = random16(NUM_LEDS);
    uint8_t colorIndex = (pos * 5 + hueShift) % 255;
    leds[pos] += ColorFromPalette(sharedPalette, colorIndex, 255);  // Add color
  }

  // Slowly rotate through palette colors
  hueShift++;

  FastLED.show();
  delay(20);
}

CRGBPalette16 blendPalettes(const CRGBPalette16& from, const CRGBPalette16& to, uint8_t amount) {
  CRGBPalette16 result;
  for (int i = 0; i < 16; i++) {
    result.entries[i] = blend(from.entries[i], to.entries[i], amount);
  }
  return result;
}
