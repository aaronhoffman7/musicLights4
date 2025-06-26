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
void updateTargetLeds(CRGB* targetLeds, float bandValue, int threshold, CRGBPalette16 palette);
void cyclePalettes();
void printBandsForPlotter();
void readTouchInputs();
void updateDynamicMaxTreble();

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

#define STROBE_DURATION 1100       // milliseconds
#define STROBE_FLASH_SPEED 25      // milliseconds
#define FLASH_DURATION 100         // milliseconds

//buttons
bool currentButtonState = HIGH;
bool lastButtonState = HIGH;
unsigned long lastButtonPollTime = 0;
const unsigned long buttonPollInterval = 15;  // ~15ms button poll interval
const int buttonPin = 32;

//music variables
const int noiseFloor = 50;
int maxTrebleValue = 0;  // variable
int maxTrebleValue2 = 1000; //start value (decays from here)
unsigned long lastTrebleTrigger = 0;
unsigned long lastBassHit = 0;
CRGB trebleColorDefault = CRGB(255, 0, 0);  // bright red for most palettes
CRGB trebleColorAlt = CRGB(255, 0, 0);    // teal-blue for alternate vibe
const int paletteCycleDuration = 20000;
const int trebleBand = 4;
int selectedBand1 = 1; // LED strip 1 (bass)
int selectedBand2 = 1; // LED strip 2 (bass)
const int baseBrightness = 12; // very dim 
const int trebleDensity = 4; // treble shows on every nth light on the strip 
const int trebleBlend = 240; // n/255 ratio of how much treble overly blends with bass base
const int trebleFadeSpeed = 2; // 2-10 longer linger = lower number  
const int bassFadeSpeed = 215; 
const unsigned long trebleCooldown = 100;  // minimum gap between treble triggers (ms)
const uint8_t trebleHitIntensity = 180;     // more subtle brightness
const float bassThresholdFactor = 0.12; //percentage of max band value that's considered within threshold
const float trebleThresholdFactor = 0.4; //
const unsigned long bassTrebleGap = 50;  // adjust gap between bass/treble flashes
const int bassDeltaThreshold = 10;  // min gap between bass triggers (ms)
const int trebleDecayMS = 120; //every n milliseconds, max treble reader is decaying x / 1023 
const int trebleDecayRaw = 4; // x / 1023
const int bassDecayMS = 100; //every n milliseconds, max treble reader is decaying x / 1023 
const int bassDecayRaw = 4; // x / 1023
int maxBandValue = 0; // max bass
float maxBandValue2 = 1023.0;  // Upper clamp for bass band value
const int paletteBlendSpeed = 20;
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
const uint8_t lingerDuration = 50;  // frames to linger (~20 x 5ms = 100ms)

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


DEFINE_GRADIENT_PALETTE(intenseRedPalette) {
  0,   255, 0, 0,     // Pure red
  128, 255, 16, 16,   // Hot red
  128, 255, 16, 16,   // Hot red
  192, 200, 0, 0,     // Deep crimson
  255, 128, 0, 0      // Darker red
};


// Assign palettes
CRGBPalette16 palette1 = indigoPurpleBluePalette;
CRGBPalette16 palette2 = tealBlueGreenPinkPalette;
CRGBPalette16 currentPalette1 = indigoPurpleBluePalette;
CRGBPalette16 targetPalette1 = indigoPurpleBluePalette;
CRGBPalette16 currentPalette2 = indigoPurpleBluePalette;
CRGBPalette16 targetPalette2 = indigoPurpleBluePalette;
CRGBPalette16 treblePalette = intenseRedPalette;



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

  // Always blend after possibly updating target palettes
  nblendPaletteTowardPalette(currentPalette1, targetPalette1, paletteBlendSpeed);
  nblendPaletteTowardPalette(currentPalette2, targetPalette2, paletteBlendSpeed);

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
      palette1 = currentPalette1;
      palette2 = currentPalette2;

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
          bounceEffect(leds1, palette1, 7, bouncePosition, bounceHueShift);
          bounceEffect(leds2, palette2, 10, bouncePosition, bounceHueShift);
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

  else if (currentMode == "music") {
    updateTargetLeds(targetLeds1, smoothBands[selectedBand1], threshold1, palette1);
    updateTargetLeds(targetLeds2, smoothBands[selectedBand2], threshold1, palette1);

    float bandValue = smoothBands[selectedBand1];
    int dynamicBlend = map(bandValue, threshold1, maxBandValue, 30, 45);
    dynamicBlend = constrain(dynamicBlend, 30, 45);
    // === Fade overlay ===
trebleOverlayFade = qsub8(trebleOverlayFade, trebleFadeSpeed);

    // Fully rebuild both strips each frame for flicker-free rendering
    for (int i = 0; i < NUM_LEDS; i++) {
        uint8_t paletteIndex = map(i, 0, NUM_LEDS - 1, 0, 255);
        CRGB baseColor = ColorFromPalette(palette1, paletteIndex, baseBrightness);  // very dim background brightness
        leds1[i] = blend(baseColor, targetLeds1[i], dynamicBlend);
        leds2[i] = blend(baseColor, targetLeds2[i], dynamicBlend);
    }

    // === Bass detection ===
    bool bassIsDominant = smoothBands[selectedBand1] > threshold1;
    if (bassIsDominant) {
        lastBassHit = now;
    }

    // === Treble detection and overlay trigger ===
// === Treble detection and segment trigger ===
bool trebleIsStrong = smoothBands[trebleBand] > trebleThreshold;
bool trebleCooldownElapsed = (now - lastTrebleTrigger > trebleCooldown);

if (trebleIsStrong && trebleCooldownElapsed) {
    trebleOverlayFade = trebleHitIntensity;
    lastTrebleTrigger = now;

    // Update position with bouncing behavior
    trebleOverlayPos += trebleSegmentDirection * (segmentLength + 1);

    if (trebleOverlayPos + segmentLength >= (NUM_LEDS * .75)) {
        trebleOverlayPos = (NUM_LEDS * .75)- segmentLength;
        trebleSegmentDirection = -1;  // Bounce back
    } else if (trebleOverlayPos <= 0) {
        trebleOverlayPos = 0;
        trebleSegmentDirection = 1;   // Bounce forward
    }
}


// Draw treble segment with fade
if (trebleOverlayFade > 0) {
  int center = trebleOverlayPos + segmentLength / 2;

  for (int i = 0; i < segmentLength; i++) {
    int ledIndex = trebleOverlayPos + i;
    if (ledIndex >= 0 && ledIndex < NUM_LEDS) {
      // Compute distance from center
      int distance = abs(i - segmentLength / 2);
      float falloff = 1.0 - (float)distance / (segmentLength / 2.0);  // 1.0 at center → 0.0 at edges
      falloff = constrain(falloff, 0.0, 1.0);

      // Apply global fade and edge falloff
      uint8_t finalBrightness = trebleOverlayFade * falloff;

      CRGB color = ColorFromPalette(treblePalette, i * (255 / segmentLength));
      color.nscale8(finalBrightness);

      leds1[ledIndex] = blend(leds1[ledIndex], color, trebleBlend);
      //leds2[ledIndex] = blend(leds2[ledIndex], color, trebleBlend);
    }
  }
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
  leds1[i] = ColorFromPalette(palette1, paletteIndex, baseBrightness);
  leds2[i] = ColorFromPalette(palette2, paletteIndex, baseBrightness);
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
      CRGB overlayColor = ColorFromPalette(palette1, i * (255 / segmentLength), trebleOverlayFade);
      leds1[ledIndex] = blend(leds1[ledIndex], overlayColor, trebleBlend);
      leds2[ledIndex] = blend(leds2[ledIndex], overlayColor, trebleBlend);  // optional
    }
  }
}


    FastLED.show();
    delay(5);
}

    else if (currentMode == "vibes") {
          vibesEffect(leds1, palette1);
          vibesEffect(leds2, palette2);
          FastLED.show();
      }
      
      else if (currentMode == "off") {
    FastLED.clear();
    FastLED.show();
      }

}


// Read MSGEQ7 values and smooth them
void readMSGEQ7() {
  // Reset the MSGEQ7
  digitalWrite(RESET_PIN, HIGH);
  delayMicroseconds(1);
  digitalWrite(RESET_PIN, LOW);

  // Read all 7 frequency bands
  for (int i = 0; i < 7; i++) {
    digitalWrite(STROBE_PIN, LOW);  
    delayMicroseconds(60);  // Allow settling (increased)
    
    int rawValue = analogRead(ANALOG_PIN); // 0–4095
    digitalWrite(STROBE_PIN, HIGH); 
    delayMicroseconds(30);

    // Rescale 0–4095 down to 0–1023
    int scaledValue = map(rawValue, 0, 4095, 0, 1023);

    // Smooth using exponential moving average
    smoothBands[i] = scaledValue * 0.8 + smoothBands[i] * 0.2;

    if (i == trebleBand) {
  // Store the current smoothed treble reading
  trebleHistory[trebleIndex] = smoothBands[i];
  trebleIndex = (trebleIndex + 1) % TREBLE_HISTORY;
}


    // Optional noise threshold (after smoothing)
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


// Update target LEDs for a given band and threshold
void updateTargetLeds(CRGB* targetLeds, float bandValue, int threshold1, CRGBPalette16 palette) {
  bool aboveThreshold = bandValue >= threshold1;
  int numLEDsLit = 0;

  if (aboveThreshold) {
    numLEDsLit = map(bandValue, threshold1, maxBandValue, minLEDsLit, NUM_LEDS);
    numLEDsLit = constrain(numLEDsLit, minLEDsLit, NUM_LEDS);
  }

  for (int i = 0; i < NUM_LEDS; i++) {
    uint8_t* timers = (targetLeds == targetLeds1) ? lingerTimers1 : lingerTimers2;

    if (i < numLEDsLit) {
      uint8_t paletteIndex = map(i, 0, NUM_LEDS - 1, 0, 255);
      targetLeds[i] = ColorFromPalette(palette, paletteIndex, 100);

      timers[i] = lingerDuration;  // Reset linger timer on hit
    } else if (timers[i] > 0) {
      timers[i]--;
      targetLeds[i].nscale8(bassFadeSpeed);  // Fade lingering LED
    } else {
      targetLeds[i] = CRGB::Black;  // Fully off if linger is over
    }
  }
}




// Cycle through predefined palettes
void cyclePalettes() {
  colorScheme1 = (colorScheme1 + 1) % 3;
  colorScheme2 = (colorScheme2 + 1) % 3;

  switch (colorScheme1) {
    case 0: targetPalette1 = indigoPurpleBluePalette; break;
    case 1: targetPalette1 = tealBlueGreenPinkPalette; break;
    case 2: targetPalette1 = greenIndigoOrangePalette; break;
  }

  switch (colorScheme2) {
   case 0: targetPalette2 = indigoPurpleBluePalette; break;
    case 1: targetPalette2 = tealBlueGreenPinkPalette; break;
    case 2: targetPalette2 = greenIndigoOrangePalette; break;
  }
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

void bounceEffect(CRGB* leds, CRGBPalette16 palette, int trailLength, int pos, uint8_t hueShift) {
  fadeToBlackBy(leds, NUM_LEDS, 30);

  for (int i = 0; i < trailLength; i++) {
    int trailPos = pos - i * bounceDirection;
    if (trailPos >= 0 && trailPos < NUM_LEDS) {
      uint8_t brightness = BRIGHTNESS / (i + 1);
      uint8_t colorIndex = (hueShift + i * 10) % 255;
      leds[trailPos] = ColorFromPalette(palette, colorIndex, 255);
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
  trebleThreshold = computeAdaptiveTrebleThreshold(trebleHistory, TREBLE_HISTORY) * 2.5;
}




void vibesEffect(CRGB* leds, CRGBPalette16 palette) {
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
    leds[pos] += ColorFromPalette(palette, colorIndex, 255);  // Add color
  }

  // Slowly rotate through palette colors
  hueShift++;

  FastLED.show();
  delay(20);
}
