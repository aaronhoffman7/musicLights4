#include <Arduino.h>
#include <FastLED.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Wire.h>
#include <string.h>
// Forward declarations for types used in prototypes
struct Cloud;
struct StaticPulse;  // optional (you already use 'struct StaticPulse*' in a proto so it's ok either way)
struct Param;
// ---- forward declares so earlier code can see these ----
struct EffectEntry { const char* name; void (*fn)(); }; // if not already visible here


void handleInputs();
void handlePotentiometer();
void handleTouchButtons();
void updateDisplay();
void readMSGEQ7();
void setMusicPalette(uint8_t idx);
void addPartySegmentOverlay_MusicDark();
void spawnSegment(int start, int len, bool isBass);
void spawnSegmentStrong(int start, int len, bool isBass, uint8_t vMax);
void addSegmentOverlay();
void dumpIOOnce();
void uiTick();
void initNewUI();

// ==== New function prototypes ====
void fx_segmentDJ();
void renderPaletteClouds(CRGB* led, bool reverseIndex,
const CRGBPalette16& pal, uint8_t baseV, Cloud* C);
void addRippleOverlay();
void renderSegmentBackground();
void spawnRipple(int center, bool isBass);
void spawnStaticPulse(bool onStrip1, int headIdx, bool dirRight);
void renderStaticPulses(struct StaticPulse* arr, CRGB* strip);

// Bank/control helpers (if you want prototypes up top as well)
int  readPotFiltered(uint8_t pin, int &lastRaw, int dead);

// === POTENTIOMETERS ===
#define POT1_PIN 32   // original 
#define POT2_PIN 15   // new second potentiometer on former BUTTON_PIN
#define EFFECT_PIN  27   // capacitive strobe toggle
#define FLASH_PIN   14   // capacitive flash
#define LASER_PIN   18
#define DATA_PIN_1 25
#define DATA_PIN_2 26
// MSGEQ7
#define STROBE_PIN 12
#define RESET_PIN  13
#define ANALOG_PIN 36

// ---- NEW CONTROL PINS (example) ----
#define BTN_A 17  // Laser toggle
#define BTN_B 19  // FX: Confetti
#define BTN_C  4  // FX: Bounce
#define BTN_D 23  // FX: DJ Segments
#define BTN_E 39  // Up
#define BTN_F 34  // Enter / Apply
#define BTN_G 35  // Down
#define BTN_H 33  // Settings / Back (hold = panic to Music)

// ---- at the top ----
constexpr bool BTN_ACTIVE_LOW = true;   // set false if using pulldown / to 3V3
constexpr bool LASER_BTN_ACTIVE_LOW = true;  // same for the laser toggle



inline bool btnPressed(int pin) {
  int lvl = digitalRead(pin);
  return BTN_ACTIVE_LOW ? (lvl == LOW) : (lvl == HIGH);
}
inline int btnIdleLevel() { return BTN_ACTIVE_LOW ? HIGH : LOW; }


int touchThreshold = 40;
int initialPotRaw = -1;
bool potLocked = true;
const int potDeadzone = 20;

// Sensitivity (right knob) commit deadband after pickup
static int  potB_lastCommitRaw = -1;
const  int  SENS_COMMIT_RAW = 120;   // ~3% of 0..4095; tweak to taste


// LED strips

#define NUM_LEDS   600
#define CHIPSET    WS2812B
#define COLOR_ORDER GRB
#define BRIGHTNESS  160

CRGB leds1[NUM_LEDS];
CRGB leds2[NUM_LEDS];

// Screen
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define SCREEN_ADDR 0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
// top of file, near the display object:
bool displayOK = false;


// ================== GLOBALS ==================

// ========== UI STATES ==========
enum UiScreen { UI_HOME, UI_SETTINGS, UI_SETTINGS_MUSIC, UI_PARAM_ADJUST, UI_FX_TWEAK };
static UiScreen ui = UI_HOME;
static uint8_t menuCursor = 0;        // index within current menu
static uint8_t musicCursor = 0;       // 0=Music Gate, 1=Bass, 2=Treble
static uint32_t uiLastActivityMs = 0; // for 6s timeout
const uint16_t UI_IDLE_MS = 6000;

// Pot locking ("pickup")
static bool potPickupLocked = true;
static int  potEntryRaw = -1;             // raw at entry to adjust
const  int  POT_PICKUP_DEAD = 40;         // << significant movement
static int  potLastCommitRaw = -1;        // last raw that produced change

// Which param is being adjusted now
enum ParamTarget { PT_NONE, PT_MUSIC_GATE, PT_BASS, PT_TREBLE, PT_PALETTE, PT_FLASHSET, PT_SENSITIVITY};
static ParamTarget activeParam = PT_NONE;

// Home -> pressing B/C/D changes FX; H (hold) forces Music mode
// Settings main menu items:
enum SettingsItem { SI_MUSIC=0, SI_PALETTE, SI_FLASH, SI_KNOBMODE, SI_COUNT };

enum PotMode : uint8_t { PM_BRIGHT_MUSIC = 0, PM_BASS_TREBLE = 1 };
static PotMode potMode = PM_BRIGHT_MUSIC;

// Independent pickup for each knob
static bool potA_pickupLocked = true, potB_pickupLocked = true;
static int  potA_entryRaw = -1,     potB_entryRaw = -1;


struct Param {
  const char* name;
  void*  ptr;       // int* or float*
  enum { P_INT, P_FLOAT, P_U8 } type;
  int    iMin, iMax, iDefault;  // for INT/U8
  float  fMin, fMax, fDefault;  // for FLOAT
  int    stepCoarse;            // e.g., 10 for gates
  int    stepFine;              // e.g., 2 for gates
};

// Debounce & edge state
struct BtnState {
  uint8_t pin;
  bool lastLevel;
  uint32_t lastChangeMs;
  bool pressed;      // current debounced state
  bool fellEdge;     // press edge this tick
  bool roseEdge;     // release edge this tick
};
static BtnState BTN[8]; // A..H (index 0..7)
enum { BI_A, BI_B, BI_C, BI_D, BI_E, BI_F, BI_G, BI_H };


// ===== Global sensitivity (0..255). 255 = current behavior, lower = less sensitive
uint8_t GATE_SENS_Q8 = 170;  // ~67% sensitivity to start
inline uint8_t sens(uint8_t n) { 
  return scale8(n, GATE_SENS_Q8);  // FastLED scale: (n * GATE_SENS_Q8) / 255
}

float smoothBands[7];
bool strobeActive = false;
// ---- forward decls for globals used before their definitions ----
extern uint8_t  CLOUD_EDGE_SOFT_KNOB;
extern uint8_t  CLOUD_SPEED_SCALE;
extern uint8_t  SPARKLE_INTENSITY;

extern uint16_t POP_FLASH_MS_K;
extern uint16_t POP_HOLD_MS_K;
extern uint16_t POP_FADE_MS_K;


// ===== Palette Clouds (Music mode, non-Dark) =====
struct Cloud {
  float center;   // 0..NUM_LEDS
  float length;   // LEDs (soft edges included)
  float speed;    // pixels per second (+right, -left)
  float wobble;   // per-cloud phase so lengths breathe a bit
};

const uint8_t CLOUD_COUNT = 4;     // how many clouds per stripmme66n6
const float   CLOUD_MIN_LEN = 180; // base size
const float   CLOUD_MAX_LEN = 280; // base size upper
float   CLOUD_EDGE    = 50;  // softness (bigger = softer edges)
const float   CLOUD_SPEED_1 =  8; // px/s for strip1 clouds (right)
const float   CLOUD_SPEED_2 = -4; // px/s for strip2 clouds (left)
const float   CLOUD_BREATHE = 0.1f; // 0..~0.3: how much clouds expand/contract

static Cloud clouds1[CLOUD_COUNT];
static Cloud clouds2[CLOUD_COUNT];
static uint32_t cloudsLastUs = 0;

// --- Auto palette cycling (Music mode) ---
bool     autoCyclePal      = false;
uint32_t nextPaletteCycle  = 0;
const uint32_t PALETTE_CYCLE_MS = 16UL * 1000UL; // 16,000 ms = 16 seconds


// ---- Arrow control modes (what Up/Down adjust) ----
enum ArrowMode : uint8_t {
  ARROW_STROBE_COLOR = 0,
  ARROW_FLASH_COLOR,
  ARROW_MUSIC_GATE,
  ARROW_BASS_GATE,
  ARROW_TREBLE_GATE,
  ARROW_LASER_GATE,
  ARROW_BRIGHTNESS,
  ARROW_MODE_COUNT
};
const char* ARROW_MODE_NAMES[] = {
  "StrobeColor", "FlashColor", "MusicGate", "BassGate", "TrebleGate", "LaserGate", "Brightness"
};
uint8_t arrowMode = ARROW_MUSIC_GATE;

// Steps/limits for arrow adjustments
const int     ARROW_STEP_THRESH   = 10;   // per keypress for thresholds
const uint8_t ARROW_STEP_BRIGHT   = 5;    // per keypress for brightness
const uint8_t BRIGHT_MIN          = 10;
const uint8_t BRIGHT_MAX          = 200;

// Defaults to restore with 'o'
const uint8_t DEFAULT_BRIGHTNESS  = BRIGHTNESS; // from your #define (160)
const int     DEFAULT_MUSIC_GATE  = 10;
const int     DEFAULT_BASS_GATE   = 350;
const int     DEFAULT_TREBLE_GATE = 350;
const uint8_t DEFAULT_FLASHSET    = 0;

// === Laser control (keyboard + button) ===
bool laserOn = false;                           // latched state
unsigned long laserPulseUntil = 0;              // momentary pulse deadline
const unsigned long LASER_PULSE_MS = 250;       // 'L' key pulse length (ms)
bool laserAutoState = false; // Auto laser state (music-driven)
unsigned long laserStrobeStart = 0;
const unsigned long LASER_STROBE_DURATION = 500;  // ms burst length
const uint16_t LASER_STROBE_SPEED = 60;           // ms per toggle (fast flash)
int  LASER_GATE_THRESH = 700;   // adjust to taste (0..900 scale like bands)
bool laserStrobeActive = false; // strobe currently running?
unsigned long lastLaserTrigger = 0;

bool debugBands = false;
unsigned long lastBandsPrint = 0;
const unsigned long BANDS_PRINT_MS = 30;

// Optional: ASCII bar width in characters (for pretty meter)
const uint8_t BAR_W = 20;


// Keyboard flash (momentary)
unsigned long flashPulseUntil = 0;
const unsigned long FLASH_PULSE_MS = 220;
// Touch strobe tuning
uint16_t TOUCH_STROBE_SPEED = 70;   // ms per toggle (feel free to tune)

// Flash fade tuning
const uint8_t  FLASH_DECAY_PER_FRAME = 14; // how fast the red fades out (0..255)
// --- Keyboard strobe + blackout ---
bool strobeFromKey = false;        // 's' toggles this
bool blackoutActive = false;       // 'd' engages this until the next key
const uint8_t BLACKOUT_FADE_STEP = 24; // higher = quicker fade per frame


// ===== Music gate (0..900 scale from readMSGEQ7 mapping) =====
int MUSIC_GATE_THRESH    = 100;  // how loud before anything shows
const uint16_t MUSIC_GATE_HOLD = 300;  // ms to keep showing after crossing threshold
static unsigned long musicGateOpenUntil = 0;

// ===== Segment POP envelope (ms) =====
const uint16_t POP_FLASH_MS = 60;   // bright flash (overwrites)
const uint16_t POP_HOLD_MS  = 250;  // hold accent color (overwrites)
const uint16_t POP_FADE_MS  = 260;  // fade back to background
const bool     POP_EDGE_WHITE = true; // white edge on segment during flash/hold

// ----- Adaptive audio (AGC) -----
static float bandFast[7]   = {0};   // fast envelope (current loudness)
static float bandFloor[7]  = {0};   // adaptive noise floor per band
static float bandCrest[7]  = {1};   // adaptive peak (headroom) per band
static uint8_t bandNorm[7] = {0};   // 0..255 normalized loudness per band

// Tunables (good starting points)
const float EMA_FAST   = 0.35f;   // fast envelope attack
const float FLOOR_UP   = 0.002f;  // floor rises very slowly
const float FLOOR_DOWN = 0.20f;   // floor falls quickly when signal drops
const float CREST_DECAY = 0.0025f; // per frame crest decay (slow)
const int   FLOOR_MARGIN_900 = 20; // deadband above floor (in your 0..900 scale)

// --- Confetti tuning ---
const uint16_t CONFETTI_SPAWN_MS   = 220; // how often to drop new dots (↑ = slower) 
const uint8_t  CONFETTI_FADE       = 4;   // per-frame fade (lower = longer trails)
const uint8_t  CONFETTI_PER_SPAWN  = 1;   // new dots per strip each spawn


// Convenience
inline int   normTo900(uint8_t n){ return (int)((n * 900 + 127) / 255); }
static uint8_t audioPeakN = 0;     // max over bands, 0..255
static bool    audioQuiet = true;  // true when everything is near floor


// ===== Dark/Music per-band gates =====
int      BASS_GATE_THRESH   = 150;  // adjust at runtime (serial keys below)
int      TREBLE_GATE_THRESH = 150;  // adjust at runtime
uint16_t BASS_HIT_DEBOUNCE  = 80;   // ms min between bass segment spawns
uint16_t TREB_HIT_DEBOUNCE  = 80;   // ms min between treble segment spawns
const uint16_t LASER_DEBOUNCE_MS = 150;  // minimum gap between strobes

// --- Bounce params ---
static int32_t b1Vel256 = 0;            // velocity in 8.8 (strip 1)
static int32_t b2Vel256 = 0;            // velocity in 8.8 (strip 2)
static uint32_t bounceLastUs = 0;       // time step
const int32_t BOUNCE_MAX_PPS     = 60;  // clamp max speed (px/s)
const int32_t BOUNCE_KICK_DV_PPS = 40;  // per-'K' speed boost (px/s)

// --- Bounce params ---
uint16_t BOUNCE_LEN        = 50;    // visible segment length (LEDs)
int32_t  BOUNCE_PPS        = 10;    // base speed (pixels per second)
int32_t  BOUNCE_JOLT_PPS   = 60;    // extra speed during jolt
const uint16_t BOUNCE_JOLT_MS    = 240;   // jolt duration (ms)
const bool     BOUNCE_EDGE_WHITE = true;  // white tips during pop
const uint8_t  BOUNCE_BASE_V     = 250;   // base brightness
// --- Bounce edge softness ---
const uint8_t BOUNCE_EDGE_SOFT = 10;   // LEDs of soft roll-off at each end
const bool    BOUNCE_USE_SMOOTHSTEP = true; // smoother (vs linear) feather

// Direction flags: true = moving right, false = moving left
static bool b1DirRight = true;
static bool b2DirRight = false; // opposite direction on strip 2

// 8.8 fixed-point state
static int32_t b1Pos256 = 0;  // head position
static int32_t b2Pos256 = 0;

// jolt state
static uint32_t joltUntilMs1 = 0;
static uint32_t joltUntilMs2 = 0;

// ---- Static pulse (flickery burst) ----
struct StaticPulse { int startIdx; bool dirRight; uint32_t startMs; bool active; };
const uint8_t MAX_PULSES = 6;

StaticPulse pulses1[MAX_PULSES] = {};
StaticPulse pulses2[MAX_PULSES] = {};

const uint16_t STATIC_PULSE_MS   = 850; // lifespan of the burst
const int32_t  STATIC_PULSE_PPS  = 200; // travel speed (px/s) ~140px over 200ms
const uint8_t  STATIC_PULSE_LEN  = 20;  // window length (px)
const uint8_t  STATIC_INTENSITY  = 100; // blend strength (0..255)

void spawnStaticPulse(bool onStrip1, int headIdx, bool dirRight);
void renderStaticPulses(StaticPulse* arr, CRGB* strip);

// Last-hit timers for independent debouncing
static unsigned long lastBassHitMs   = 0;
static unsigned long lastTrebleHitMs = 0;


// Tap cursors & step distance
static int bassCursor   = 0;
static int trebleCursor = NUM_LEDS - 1;
const int BASS_STEP     = 28;   // advance per 'N' tap
const int TREBLE_STEP   = 19;   // advance per 'C' tap
const int BASS_SEG_LEN  = 40;   // segment length for bass burst
const int TREB_SEG_LEN  = 26;   // segment length for treble burst


struct Ripple {
  int center;
  unsigned long startMs;
  bool bass; // true = N, false = C
  bool active;
};
struct Segment {
  int start;
  int length;
  unsigned long startMs;
  bool bass;    // true = N, false = C
  bool active;
  uint8_t vMax; // NEW: per-hit max brightness (0..255)
};


static const uint8_t MAX_RIPPLES  = 10;
static const uint8_t MAX_SEGMENTS = 8;
static Ripple  ripples[MAX_RIPPLES];
static Segment segments[MAX_SEGMENTS];

// Ripple dynamics
const float RIPPLE_SPEED_PX_PER_MS = 0.18f; // radius grows per ms
const uint16_t RIPPLE_FADE_MS      = 900;   // lifespan
const uint8_t RIPPLE_WIDTH         = 10;    // ring thickness (px)

// --- Dark (Music) → Party segment-pop engine ---
const uint8_t DARK_PALETTE_INDEX = 5;      // your "Dark" entry in musicPalettes[]
static unsigned long lastMusicHitMs = 0;
const uint16_t HIT_DEBOUNCE_MS = 80;       // min ms between spawns

// Separate cursors for MUSIC_MODE so they don't clash with DJ cursors
static int musicBassCursor   = 0;
static int musicTrebleCursor = NUM_LEDS - 1;

// Reuse your existing POP envelope constants:
// POP_FLASH_MS, POP_HOLD_MS, POP_FADE_MS, POP_EDGE_WHITE

// How far above the gate (0..1) using normalized units
inline float overGate01(uint8_t n, uint8_t gateN) {
  if (n <= gateN) return 0.0f;
  float r = float(n - gateN) / float(255 - gateN);
  // a touch of “punch” so big hits feel bigger
  return sqrtf(constrain(r, 0.f, 1.f));
}

// Per-hit brightness (vMax) 130..255, normalized inputs
inline uint8_t hitV(uint8_t n, uint8_t gateN) {
  float x = overGate01(n, gateN);                      // 0..1
  return uint8_t(130 + x * 125);                       // 130..255
}

// Scale segment length by hit size: baseLen .. baseLen+boost
inline int scaledLen(uint8_t n, uint8_t gateN, int baseLen, int boost) {
  float x = overGate01(n, gateN);
  return baseLen + int(x * boost + 0.5f);
}

// compatibility wrappers so existing call sites keep working
static inline uint8_t hitV_u8(uint8_t n, uint8_t gateN) {
  return hitV(n, gateN);
}
static inline int scaledLen_u8(uint8_t n, uint8_t gateN, int baseLen, int boost) {
  return scaledLen(n, gateN, baseLen, boost);
}


bool flashHeldTouch = false;

// ---- Flash color SETS (two colors: strip1, strip2) ----
struct FlashSet { CRGB s1, s2; const char* name; };
const FlashSet FLASH_SETS[] = {
  { CRGB::Red,   CRGB::White,   "White/White"   },
  { CRGB::DeepPink,    CRGB::Aqua,    "Blue/Aqua"     },
  { CRGB::Indigo, CRGB::Blue,"Magenta/DPink" },
  { CRGB::Yellow,  CRGB::Gold,  "Yellow/Orange" },
  { CRGB::Green,   CRGB::Teal,"Green/Lime"   },
  { CRGB::Purple,  CRGB::BlueViolet,"Purple/BV"   },
  { CRGB::Aqua,    CRGB::Cyan,    "Aqua/Cyan"     },
  { CRGB::Gold,    CRGB::Red,     "Gold/Red"      },
};
const uint8_t FLASH_SET_COUNT = sizeof(FLASH_SETS)/sizeof(FLASH_SETS[0]);
uint8_t flashSetIdx = 5;

// ---- Strobe color SETS (two colors: strip1, strip2) ----
struct StrobeSet { CRGB s1, s2; const char* name; };
const StrobeSet STROBE_SETS[] = {
  { CRGB::White,     CRGB::Blue,      "White/Blue"   }, // current
  { CRGB::White,     CRGB::Red,       "White/Red"    },
  { CRGB::Gold,      CRGB::White,     "Gold/White"   },
  { CRGB::Aqua,      CRGB::Cyan,      "Aqua/Cyan"    },
  { CRGB::Teal,      CRGB::Magenta,   "Teal/Magenta" },
  { CRGB::Indigo,    CRGB::Red,       "Indigo/Red"   },
};
const uint8_t STROBE_SET_COUNT = sizeof(STROBE_SETS)/sizeof(STROBE_SETS[0]);
uint8_t strobeSetIdx = 0;  // <-- default strobe set on boot


// Arrow-key parser: ESC [ A/B
enum EscState { ESC_IDLE, ESC_SEEN, ESC_BRACKET };
static EscState escState = ESC_IDLE;



// ==== MUSIC PALETTES (1..9) ====
uint8_t musicPaletteIndex = 2; // 0-based
CRGBPalette16 startPal;
CRGBPalette16 targetPal;
unsigned long blendStartTime = 0;
unsigned long blendDuration  = 250;
bool blending = false;

const CRGBPalette16 PALETTE_TEAL_MAGENTA(
  CRGB::Teal, CRGB::Teal, CRGB::Aqua, CRGB::Aqua,
  CRGB::DarkTurquoise, CRGB::DarkTurquoise, CRGB::DeepSkyBlue, CRGB::DeepSkyBlue,
  CRGB::Blue, CRGB::Blue, CRGB::MediumVioletRed, CRGB::MediumVioletRed,
  CRGB::Magenta, CRGB::Magenta, CRGB::DeepPink, CRGB::DeepPink
);

const CRGBPalette16 PALETTE_MOLTEN_AURORA(
  CRGB::Black,       CRGB::DarkRed,     CRGB::DarkRed, 
  CRGB::Maroon,      CRGB::Crimson,     CRGB::OrangeRed,
  CRGB::DarkOrange,  CRGB::Gold,
  CRGB::DarkTurquoise, CRGB::Teal,
  CRGB::Indigo,      CRGB::DarkBlue,
  CRGB::Black,       CRGB::DarkRed, 
  CRGB::OrangeRed,   CRGB::Gold
);

const CRGBPalette16 PALETTE_NEON_WAVE(
  CRGB::Black,     CRGB::DeepPink,   CRGB::Magenta, 
  CRGB::BlueViolet,CRGB::DarkBlue,   CRGB::Cyan,
  CRGB::Aqua,      CRGB::LimeGreen,
  CRGB::Chartreuse,CRGB::Yellow,
  CRGB::Orange,    CRGB::Red,
  CRGB::Black,     CRGB::DeepPink, 
  CRGB::Cyan,      CRGB::Yellow
);

// --- Dark mode strip palettes ---
const CRGBPalette16 PALETTE_DARK_BASS(
  CRGB::Indigo, CRGB::Indigo, CRGB::Indigo, CRGB::Indigo,
  CRGB::Indigo, CRGB::Indigo, CRGB::Indigo, CRGB::Indigo,
  CRGB::Indigo, CRGB::Indigo, CRGB::Indigo, CRGB::Indigo,
  CRGB::Indigo, CRGB::Indigo, CRGB::Indigo, CRGB::Indigo
);

const CRGBPalette16 PALETTE_DARK_TREBLE(
  CRGB::Red, CRGB::Red, CRGB::Red, CRGB::Red,
  CRGB::Red, CRGB::Red, CRGB::Red, CRGB::Red,
  CRGB::Red, CRGB::Red, CRGB::Red, CRGB::Red,
  CRGB::Red, CRGB::Red, CRGB::Red, CRGB::Red
);



// Pure black palette (all off)
const CRGBPalette16 PALETTE_BLACK(
  CRGB::Black, CRGB::Black, CRGB::Black, CRGB::Black,
  CRGB::Black, CRGB::Black, CRGB::Black, CRGB::Black,
  CRGB::Black, CRGB::Black, CRGB::Black, CRGB::Black,
  CRGB::Black, CRGB::Black, CRGB::Black, CRGB::Black
);

// Per-strip palettes (used in BOTH Music & DJ Segment when Dark is selected)
const CRGBPalette16* DARK_SEGMENT_PAL_STRIP1 = &PALETTE_DARK_BASS;   // Indigo
const CRGBPalette16* DARK_SEGMENT_PAL_STRIP2 = &PALETTE_DARK_TREBLE; // Red


const CRGBPalette16 musicPalettes[] = {
  RainbowColors_p,        // 1
  PartyColors_p,          // 2
  OceanColors_p,          // 3
  ForestColors_p,         // 4
  HeatColors_p,           // 5
  PALETTE_BLACK,          // 6
  PALETTE_MOLTEN_AURORA,  // 7 new
  PALETTE_NEON_WAVE,      // 8 new
  PALETTE_TEAL_MAGENTA    // 9
};

// === Hard-coded accent colors per palette (index must match musicPalettes[]) ===
// Tune these to taste per palette for consistent stage vibes.
const CRGB BASS_ACCENT[9] = {
  CRGB::BlueViolet,        // Rainbow
  CRGB::Blue,   // Party
  CRGB::Indigo, // Ocean
  CRGB::Indigo,   // Forest
  CRGB::Blue,       // Heat
  CRGB::Indigo,     // Dark
  CRGB::Blue,        // Lava
  CRGB::Red,      // RainbowStripe
  CRGB::Indigo       // TealMagenta
};

const CRGB TREBLE_ACCENT[9] = {
  CRGB::Red,        // Rainbow
  CRGB::Red,   // Party
  CRGB::Red, // Ocean
  CRGB::Red,   // Forest
  CRGB::Violet,       // Heat
  CRGB::Red,     // Dark
  CRGB::Red,        // Lava
  CRGB::Red,      // RainbowStripe
  CRGB::Red       // TealMagenta
};

// Small, fast filter (8x average). ADC is cheap; this removes shimmer.
static inline int readAdjPot() {
  uint32_t sum = 0;
  for (uint8_t i=0;i<16;i++) sum += analogRead(POT1_PIN);
  return (int)(sum >> 3);
}

inline uint8_t gateToNorm255(int thr) {
  thr = constrain(thr, 0, 900);
  return (uint8_t)map(thr, 0, 900, 0, 255);
}


// Blend helper: mix base toward accent by 'strength' (0..255)
inline CRGB blendToward(const CRGB& base, const CRGB& accent, uint8_t strength) {
  CRGB out = base;
  nblend(out, accent, strength); // FastLED's perceptual blend
  return out;
}

// Soft overlay blend: mix src into dst (0..255)
// 255 ~= overwrite, 0 = no change
inline void mixOverlay(CRGB& dst, const CRGB& src, uint8_t strength) {
  nblend(dst, src, strength);
}



const char* musicPaletteNames[] = {
  "Rainbow","Party","Ocean","Forest","Heat","Dark","Lava","RainbowStripe","TealMagenta"
};
const uint8_t MUSIC_PALETTE_COUNT = sizeof(musicPalettes)/sizeof(musicPalettes[0]);

static CRGBPalette16 currentPal = musicPalettes[0];
// === Flicker engine state (shared by DJ Segments) ===
static uint32_t flickerTickMs = 0;
static uint8_t  flickerSeed   = 0;

// Call every frame before drawing bursts to advance flicker
inline void advanceFlicker() {
  uint32_t now = millis();
  // ~30 Hz base flicker; jitter a bit so it doesn’t feel mechanical
  static uint8_t jitter = 0;
  if (now - flickerTickMs > (28 + jitter)) {
    flickerTickMs = now;
    flickerSeed = random8();
    jitter = random8(0, 10); // 0–9 ms
  }
}

enum Mode { MUSIC_MODE, FX_MODE };
Mode currentMode = FX_MODE;


// ====== FX FOR MANUAL MODE ======
void fx_confetti();
void fx_bounce();
void fx_rainbow();
void fx_paletteFlow();
void fx_segmentDJ();

// Index order for explicit key mapping
enum {
  FX_CONFETTI = 0,
  FX_BOUNCE   = 1,
  FX_RAINBOW  = 2,
  FX_SEGMENT_DJ = 3
};

int currentEffect = FX_SEGMENT_DJ;

// --- Pot routing roles ---
enum PotRole : uint8_t {
  PR_NONE,
  PR_HOME_BRIGHT,
  PR_BOUNCE_LEN,
  PR_BOUNCE_SPEED,
  PR_CONFETTI_BRIGHT
};
static PotRole potRole = PR_NONE;
static int     potPickupRaw = -1;
static bool    potPickup    = true;

// Bounce: which param the pot controls when in Bounce (off-Home)
static bool bouncePotTargetsLen = true; // default Length (tap F to toggle)

inline PotRole computePotRole() {
  if (ui == UI_PARAM_ADJUST) return PR_NONE;          // dedicated adjust owns the pot
  if (ui == UI_HOME)          return PR_HOME_BRIGHT;  // always brightness on Home

  // FX tweak screen: route by current FX
  if (ui == UI_FX_TWEAK && currentMode == FX_MODE) {
    if (currentEffect == FX_BOUNCE)
      return bouncePotTargetsLen ? PR_BOUNCE_LEN : PR_BOUNCE_SPEED;
    if (currentEffect == FX_CONFETTI)
      return PR_CONFETTI_BRIGHT;
  }
  return PR_NONE;
}



EffectEntry FX[] = {
  { "Confetti",    fx_confetti  },
  { "Bounce",      fx_bounce    },
  { "Rainbow",     fx_rainbow   },
  { "DJ Segments", fx_segmentDJ }
};

const int FX_COUNT = sizeof(FX) / sizeof(FX[0]);

void printBandsLine() {
  int peak = 0, sum = 0;
  Serial.print("Bands: ");
  for (int i = 0; i < 7; i++) {
    int v = (int)smoothBands[i];
    if (v > peak) peak = v;
    sum += v;
    Serial.print(v);
    if (i < 6) Serial.print(", ");
  }
  float avg = sum / 7.0f;
  Serial.print("  | peak=");
  Serial.print(peak);
  Serial.print(" avg=");
  Serial.println(avg, 1);
}

void printBandsBars() {
  // Simple ASCII bar graph (0..900 -> 0..BAR_W)
  for (int i = 0; i < 7; i++) {
    int v = constrain((int)smoothBands[i], 0, 900);
    int n = map(v, 0, 900, 0, BAR_W);
    Serial.printf("%d: ", i);
    for (int k = 0; k < n; k++) Serial.print('#');
    for (int k = n; k < BAR_W; k++) Serial.print(' ');
    Serial.printf(" | %d\n", v);
  }
  Serial.println();
}



// ============== SETUP ==============
void setup() {
  Serial.begin(115200);

    // Start I2C explicitly on ESP32 default pins
  Wire.begin(21, 22);              // SDA=21, SCL=22
  Wire.setClock(400000);           // (optional) fast mode

  // Try both 0x3C and 0x3D so we don't get stuck on the wrong addr
  displayOK = display.begin(SSD1306_SWITCHCAPVCC, 0x3C) ||
              display.begin(SSD1306_SWITCHCAPVCC, 0x3D);

  if (!displayOK) {
    Serial.println("OLED init failed — continuing headless.");
  } else {
    display.clearDisplay();
    display.display();
    updateDisplay();
  }

  // === Add in setup() after Serial.begin(...) ===
analogReadResolution(12);                      // ESP32 ADC 0..4095
analogSetPinAttenuation(ANALOG_PIN, ADC_11db); // up to ~3.6V on ADC1 pins
analogSetPinAttenuation(POT1_PIN, ADC_11db);
analogSetPinAttenuation(POT2_PIN, ADC_11db);  // GPIO 15 is ADC2; fine if WiFi is off

  display.clearDisplay();
  display.display();
  updateDisplay();
  
    initNewUI();


  bounceLastUs = micros();
b1Pos256 = 0;                                           // start at left
b2Pos256 = (int32_t)(NUM_LEDS - BOUNCE_LEN) << 8;       // start at right
b1Vel256 =  (BOUNCE_PPS * 256);                         // move →
b2Vel256 = -(BOUNCE_PPS * 256);                         // move ←
b1DirRight = true;
b2DirRight = false;

  FastLED.addLeds<CHIPSET, DATA_PIN_1, COLOR_ORDER>(leds1, NUM_LEDS);
  FastLED.addLeds<CHIPSET, DATA_PIN_2, COLOR_ORDER>(leds2, NUM_LEDS);
  FastLED.setBrightness(BRIGHTNESS);

  // --- init drifting palette clouds ---
auto initClouds = [](Cloud* C, float baseSpeed){
  for (uint8_t i=0;i<CLOUD_COUNT;i++){
    C[i].center = random16(NUM_LEDS);
    C[i].length = random(CLOUD_MIN_LEN, CLOUD_MAX_LEN);
    C[i].speed  = baseSpeed * (0.7f + (random8()/255.0f)*0.6f); // ±30% variation
    C[i].wobble = random16(); // random phase
  }
};
initClouds(clouds1, CLOUD_SPEED_1);
initClouds(clouds2, CLOUD_SPEED_2);
cloudsLastUs = micros();

  pinMode(STROBE_PIN, OUTPUT);
  pinMode(RESET_PIN, OUTPUT);
  pinMode(ANALOG_PIN, INPUT);

  digitalWrite(STROBE_PIN, HIGH);
  digitalWrite(RESET_PIN,  LOW);

  pinMode(LASER_PIN, OUTPUT);
  digitalWrite(LASER_PIN, LOW);
}

// ============== LOOP ==============
void loop() {
  handleInputs();
  uiTick();
  CLOUD_EDGE = (float)map(CLOUD_EDGE_SOFT_KNOB, 10, 200, 20, 90); // softer ↔ sharper edges

  handlePotentiometer();
  handleTouchButtons();
  laserAutoState = false;

  // Auto-cycle palettes in Music mode
if (currentMode == MUSIC_MODE && autoCyclePal && millis() >= nextPaletteCycle) {
  uint8_t nextIdx = (musicPaletteIndex + 1) % MUSIC_PALETTE_COUNT;
  setMusicPalette(nextIdx);                   // uses your existing smooth blend
  nextPaletteCycle += PALETTE_CYCLE_MS;       // schedule the next hop
}

  // ===== Blackout short-circuit =====
if (blackoutActive) {
  fadeToBlackBy(leds1, NUM_LEDS, BLACKOUT_FADE_STEP);
  fadeToBlackBy(leds2, NUM_LEDS, BLACKOUT_FADE_STEP);
  FastLED.show();

  // ensure outputs follow blackout too
  digitalWrite(LASER_PIN, LOW);
  return;
}


  if (currentMode == MUSIC_MODE) {
    readMSGEQ7();
    fx_paletteFlow();   // music-reactive only in MUSIC_MODE
  } else {
    FX[currentEffect].fn(); // Manual FX mode
  }

  // ===== Touch overlays =====
const unsigned long nowMs = millis();
static uint8_t flashLevel = 0;  // 0..255
bool flashPressed = flashHeldTouch || (nowMs < flashPulseUntil);
if (flashPressed) {
  flashLevel = 255; // instant to full on press
} else if (flashLevel > 0) {
  flashLevel = (flashLevel > FLASH_DECAY_PER_FRAME)
               ? (flashLevel - FLASH_DECAY_PER_FRAME)
               : 0; // smooth falloff
}
if (flashLevel > 0) {
  CRGB c1 = FLASH_SETS[flashSetIdx].s1;
  CRGB c2 = FLASH_SETS[flashSetIdx].s2;
  c1.nscale8_video(flashLevel);
  c2.nscale8_video(flashLevel);
  for (int i = 0; i < NUM_LEDS; i++) {
    nblend(leds1[i], c1, flashLevel); // strip 1 uses set.s1
    nblend(leds2[i], c2, flashLevel); // strip 2 uses set.s2
  }
}


// --- LASER OUTPUT DRIVE ---
bool autoLaserNow = false;

// Always service the strobe, regardless of mode
if (laserStrobeActive) {
  unsigned long elapsed = millis() - laserStrobeStart;
  if (elapsed >= LASER_STROBE_DURATION) {
    laserStrobeActive = false;
  } else {
    // Toggle on/off at strobe speed
    autoLaserNow = ((elapsed / LASER_STROBE_SPEED) % 2) == 0;
  }
}

  if (debugBands && millis() - lastBandsPrint >= BANDS_PRINT_MS) {
    lastBandsPrint = millis();
    printBandsLine();    // one-line numbers
    // printBandsBars(); // uncomment if you also want ASCII bars each tick
  }

bool laserNow = laserOn || (millis() < laserPulseUntil) || autoLaserNow || laserAutoState;
digitalWrite(LASER_PIN, laserNow ? HIGH : LOW);

// -- Effect (touch) strobe: toggle while held
// -- Effect strobe: either touch OR keyboard 's' toggle
bool strobeNow = (strobeActive /*touch*/ || strobeFromKey);
if (strobeNow) {
  // 50% duty: on/off every TOUCH_STROBE_SPEED ms
  bool strobeOn = (((nowMs / TOUCH_STROBE_SPEED) & 1) == 0);

  if (strobeOn) {
    // ON frame uses selected strobe palette
    const CRGB s1 = STROBE_SETS[strobeSetIdx].s1;
    const CRGB s2 = STROBE_SETS[strobeSetIdx].s2;
    fill_solid(leds1, NUM_LEDS, s1);
    fill_solid(leds2, NUM_LEDS, s2);
  } else {
    // OFF frame -> force black so background can't bleed through
    fill_solid(leds1, NUM_LEDS, CRGB::Black);
    fill_solid(leds2, NUM_LEDS, CRGB::Black);
  }
}



FastLED.show();


}

// ============== TOUCH / BUTTONS ==============
void handleTouchButtons() {
  strobeActive   = (touchRead(EFFECT_PIN) < touchThreshold);
  flashHeldTouch = (touchRead(FLASH_PIN)  < touchThreshold);


}



// ============== POT ==============
void handlePotentiometer() {
  // Dedicated param-adjust screen owns the left knob; ignore both to avoid conflicts
  if (ui == UI_PARAM_ADJUST) return;

  // --- Read both knobs raw ---
  int rawA = analogRead(POT1_PIN); // left knob (legacy POT)
  int rawB = analogRead(POT2_PIN); // right knob (new POT)

  // --- Pickup (per-knob) ---
  if (potA_entryRaw < 0) potA_entryRaw = rawA;
  if (potB_entryRaw < 0) potB_entryRaw = rawB;

  bool allowA = true, allowB = true;
  if (potA_pickupLocked && abs(rawA - potA_entryRaw) < POT_PICKUP_DEAD) allowA = false;
  if (potB_pickupLocked && abs(rawB - potB_entryRaw) < POT_PICKUP_DEAD) allowB = false;
  if (allowA) potA_pickupLocked = false;
  if (allowB) potB_pickupLocked = false;

  // Helpers
  auto mapBright = [&](int r)->int {
    return constrain(map(r, 0, 4095, BRIGHT_MIN, BRIGHT_MAX), BRIGHT_MIN, BRIGHT_MAX);
  };
  auto mapGate = [&](int r)->int {
    return constrain(map(r, 0, 4095, 0, 900), 0, 900);
  };

  // === LEFT KNOB (keeps your FX tweak routing when in FX tweak UI) ===
  PotRole newRole = computePotRole(); // your existing router (HOME=brightness, FX tweak, etc.)
  if (newRole != potRole) { potRole = newRole; potPickupRaw = -1; potPickup = true; }

  if (potRole == PR_HOME_BRIGHT && allowA) {
    FastLED.setBrightness(mapBright(rawA));
  } else if (potRole == PR_CONFETTI_BRIGHT && allowA) {
    FastLED.setBrightness(mapBright(rawA));
  } else if (potRole == PR_BOUNCE_LEN && allowA) {
    uint16_t L = (uint16_t)map(rawA, 0, 4095, 10, 200);
    BOUNCE_LEN = constrain(L, 10, 200);
  } else if (potRole == PR_BOUNCE_SPEED && allowA) {
    int v = map(rawA, 0, 4095, 2, 120);
    BOUNCE_PPS = constrain(v, 2, 120);
  } else {
    // When not in a special FX tweak role, apply the selected "Knob Mode" default for LEFT
    if (allowA) {
      if (potMode == PM_BRIGHT_MUSIC) {
        FastLED.setBrightness(mapBright(rawA)); // default: brightness
      } else { // PM_BASS_TREBLE
        BASS_GATE_THRESH = mapGate(rawA);
      }
    }
  }

  // === RIGHT KNOB (always mapped by "Knob Mode", unless in Param Adjust) ===
  // === RIGHT KNOB (default: Sensitivity %) ===
if (allowB) {
  // pickup already handled above (potB_pickupLocked)
  if (potB_lastCommitRaw < 0) potB_lastCommitRaw = rawB;

  // only update when we move a "significant" amount from the last commit
  if (abs(rawB - potB_lastCommitRaw) >= SENS_COMMIT_RAW) {
    // 0..4095 -> 0..100%
    int pct = constrain(map(rawB, 0, 4095, 0, 100), 0, 100);
    // scale 0..100% -> 0..255 (FastLED scale8 factor)
    GATE_SENS_Q8 = (uint8_t)((pct * 255 + 50) / 100);  // rounded

    potB_lastCommitRaw = rawB;   // remember where we committed from
    uiLastActivityMs   = millis();
    // optional: quick serial HUD
    Serial.printf("Sensitivity=%d%% (Q8=%u)\n", pct, GATE_SENS_Q8);
  }
}

}




// ============== MSGEQ7 (for MUSIC_MODE only) ==============
void readMSGEQ7() {

    // ----- NEW: flush ADC channel switch noise -----
  (void)analogRead(ANALOG_PIN);
  delayMicroseconds(5);
  (void)analogRead(ANALOG_PIN);
  delayMicroseconds(5);

  // Idle STROBE high before reset
  digitalWrite(STROBE_PIN, HIGH);
  digitalWrite(RESET_PIN,  HIGH);  delayMicroseconds(5);
  digitalWrite(RESET_PIN,  LOW);   delayMicroseconds(5);

  uint8_t newPeakN = 0;
  bool allNearFloor = true;

  for (int i = 0; i < 7; i++) {
    // LOW time selects the band and lets it settle
    digitalWrite(STROBE_PIN, LOW);
    delayMicroseconds(30);

    // Throw away first read after the mux step
    (void)analogRead(ANALOG_PIN);
    int raw = analogRead(ANALOG_PIN);

    // HIGH time before next band
    digitalWrite(STROBE_PIN, HIGH);
    delayMicroseconds(36);

    // Map ADC -> your historical scale (0..900) so we can reuse everything
    float val900 = (float)map(raw, 0, 4095, 0, 900);

    // ----- fast envelope -----
    bandFast[i] = bandFast[i] + EMA_FAST * (val900 - bandFast[i]);

    // ----- adaptive floor (tracks the "quiet" baseline) -----
    if (bandFast[i] > bandFloor[i]) {
      // floor creeps up very slowly toward long-term level
      bandFloor[i] += FLOOR_UP * (bandFast[i] - bandFloor[i]);
    } else {
      // floor drops quickly when the scene gets quieter
      bandFloor[i] += FLOOR_DOWN * (bandFast[i] - bandFloor[i]);
    }
    // keep floor sane
    if (bandFloor[i] < 0)   bandFloor[i] = 0;
    if (bandFloor[i] > 880) bandFloor[i] = 880;

    // ----- adaptive crest (headroom tracker / slow decay) -----
    if (bandFast[i] > bandCrest[i]) {
      bandCrest[i] = bandFast[i];
    } else {
      bandCrest[i] = bandCrest[i] - CREST_DECAY * (bandCrest[i] - bandFloor[i]);
    }
    if (bandCrest[i] < bandFloor[i] + 10) bandCrest[i] = bandFloor[i] + 10;

    // ----- normalized 0..255 loudness above floor -----
    float f = bandFast[i] - (bandFloor[i] + FLOOR_MARGIN_900);
    float d = (bandCrest[i] - (bandFloor[i] + FLOOR_MARGIN_900));
    uint8_t n = 0;
    if (d > 5.0f && f > 0.0f) {
      float x = f / d;           // 0..1
      x = sqrtf(x);              // compand for punchier hits
      if (x > 1.0f) x = 1.0f;
      n = (uint8_t)(x * 255.0f + 0.5f);
    }
    bandNorm[i] = n;
    if (n > newPeakN) newPeakN = n;

    // legacy smoothed bands (still helpful in a few places)
    smoothBands[i] = 0.7f * smoothBands[i] + 0.3f * val900;

    // quiet detector (near floor)
    if (n > 10) allNearFloor = false;
  }

  audioPeakN = newPeakN;
  audioQuiet = allNearFloor;
}


// ============== FX (manual) ==============
void fx_confetti() {
  // trails
  fadeToBlackBy(leds1, NUM_LEDS, CONFETTI_FADE);
  fadeToBlackBy(leds2, NUM_LEDS, CONFETTI_FADE);

  // slower, time-based spawning (consistent regardless of FPS)
  EVERY_N_MILLISECONDS(CONFETTI_SPAWN_MS) {
    for (uint8_t i = 0; i < CONFETTI_PER_SPAWN; i++) {
      leds1[random16(NUM_LEDS)] += CHSV(random8(), 200, 255);
      leds2[random16(NUM_LEDS)] += CHSV(random8(), 200, 255);
    }
  }
}


void fx_bounce() {
  // Black baseline (only the segment is lit)
  fill_solid(leds1, NUM_LEDS, CRGB::Black);
  fill_solid(leds2, NUM_LEDS, CRGB::Black);

  // --- Blend currentPal if needed (so 1–9 cross-fade in Bounce) ---
  if (blending) {
    unsigned long now = millis();
    unsigned long elapsed = now - blendStartTime;
    if (elapsed >= blendDuration) {
      currentPal = targetPal;
      blending = false;
    } else {
      float progress = (float)elapsed / (float)blendDuration;
      for (int i = 0; i < 16; i++) {
        currentPal[i].r = startPal[i].r + (targetPal[i].r - startPal[i].r) * progress;
        currentPal[i].g = startPal[i].g + (targetPal[i].g - startPal[i].g) * progress;
        currentPal[i].b = startPal[i].b + (targetPal[i].b - startPal[i].b) * progress;
      }
    }
  }

  // --- Time step ---
  uint32_t nowUs = micros();
  uint32_t dtUs  = nowUs - bounceLastUs;
  if (dtUs > 200000) dtUs = 200000;   // clamp stalls
  bounceLastUs = nowUs;

  // Speeds (px/s), with temporary jolt while active
  uint32_t nowMs = millis();
  int32_t v1_pps = BOUNCE_PPS + ((nowMs < joltUntilMs1) ? BOUNCE_JOLT_PPS : 0);
  int32_t v2_pps = BOUNCE_PPS + ((nowMs < joltUntilMs2) ? BOUNCE_JOLT_PPS : 0);

  // 8.8 delta = v * dt
  int32_t dv1_256 = (int32_t)(( (int64_t)v1_pps * (int64_t)dtUs * 256) / 1000000LL);
  int32_t dv2_256 = (int32_t)(( (int64_t)v2_pps * (int64_t)dtUs * 256) / 1000000LL);

  // Travel range so the whole block stays on-strip
  const int32_t headMax = (int32_t)(NUM_LEDS - BOUNCE_LEN) << 8;

  // --- Integrate with real bounce (flip direction at ends) ---
  auto stepBounce = [&](int32_t &pos, int32_t dv, bool &dirRight){
    // move in current direction
    pos += dirRight ? dv : -dv;

    // reflect if we crossed either end (preserve overshoot)
    if (pos < 0) {
      int32_t over = -pos;
      pos = over;               // reflect distance back into strip
      dirRight = true;          // now going right
    } else if (pos > headMax) {
      int32_t over = pos - headMax;
      pos = headMax - over;     // reflect distance back into strip
      dirRight = false;         // now going left
    }
  };

  stepBounce(b1Pos256, dv1_256, b1DirRight);  // strip 1
  stepBounce(b2Pos256, dv2_256, b2DirRight);  // strip 2

  // Draw the segments using the current palette (only the block is lit)
  auto drawSegment = [&](CRGB *arr, int headIdx, bool forward, bool popPhase){
  const int L = (int)BOUNCE_LEN;
  for (int o = 0; o < L; ++o) {
    int p = forward ? (headIdx + o) : (headIdx - o);
    if (p < 0 || p >= NUM_LEDS) continue;

    // --- edge feather weight (0..1), 1 in the middle, 0 at edges ---
    // distance to nearest edge of the lit block
    int dEdge = min(o, L - 1 - o);

    float w;
    if (dEdge >= BOUNCE_EDGE_SOFT) {
      w = 1.0f; // center plateau
    } else {
      float t = (float)dEdge / max(1, (int)BOUNCE_EDGE_SOFT); // 0..1 toward interior
      if (BOUNCE_USE_SMOOTHSTEP) {
        // smootherstep: 3t^2 - 2t^3
        w = (t * t) * (3.0f - 2.0f * t);
      } else {
        w = t; // linear feather as fallback
      }
    }

    // base palette color with feathered brightness
    uint8_t palIdx = (uint8_t)((o * (256 / max(1, L - 1))) + (millis() >> 2));
    uint8_t V = scale8_video(BOUNCE_BASE_V, (uint8_t)(w * 255));

    CRGB c = ColorFromPalette(currentPal, palIdx, V);

    // artful pop while jolting (kept subtle so edge still feathers)
    if (popPhase) {
      nblend(c, CRGB::White, 48);
      c.fadeLightBy(24);
    }

    // write (no trail baseline is already black)
    arr[p] = c;
  }

  // Optional white tips only during pop (kept, but they’ll sit on top of feather)
  if (BOUNCE_EDGE_WHITE && popPhase) {
    int tipA = forward ? headIdx : (headIdx - L + 1);
    int tipB = forward ? (headIdx + L - 1) : headIdx;
    if (tipA >= 0 && tipA < NUM_LEDS) arr[tipA] = CRGB::White;
    if (tipB >= 0 && tipB < NUM_LEDS) arr[tipB] = CRGB::White;
  }
};

  int h1 = (int)(b1Pos256 >> 8);
  int h2 = (int)(b2Pos256 >> 8);

  bool pop1 = (nowMs < joltUntilMs1);
  bool pop2 = (nowMs < joltUntilMs2);

  // Forward flag for drawing = current direction
  drawSegment(leds1, h1, /*forward*/ b1DirRight, pop1);
  drawSegment(leds2, h2, /*forward*/ b2DirRight, pop2);
  renderStaticPulses(pulses1, leds1);
  renderStaticPulses(pulses2, leds2);

}






void fx_rainbow() {

  static uint8_t hue = 0;
  hue++;
  fill_rainbow(leds1, NUM_LEDS, hue, 4);
  fill_rainbow(leds2, NUM_LEDS, hue+64, 4);
}

// Format the current value of a Param into a small buffer
static void formatParamValue(const Param* p, char* out, size_t n) {
  if (p->type == Param::P_INT)    snprintf(out, n, "%d",  *((int*)p->ptr));
  else if (p->type == Param::P_U8)snprintf(out, n, "%u",  *((uint8_t*)p->ptr));
  else                            snprintf(out, n, "%.2f",*((float*)p->ptr));
}

// ============== DISPLAY ==============
void updateDisplay() {
  if (!displayOK) return;  // <- add this line
  display.clearDisplay();
  display.setCursor(0,0);
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.println("Visualizer");
  display.print("Mode: ");
  display.println(currentMode == MUSIC_MODE ? "Music" : "FX");
  display.print("AutoPal: ");
  display.println(autoCyclePal ? "On" : "Off");

  if (currentMode == MUSIC_MODE) {
    display.println("Effect: PaletteFlow");
    display.print("Palette: ");
    display.println(musicPaletteNames[musicPaletteIndex]);
  } else {
    display.print("Effect: ");
    display.println(FX[currentEffect].name);
    display.print("Palette: ");
    display.println(musicPaletteNames[musicPaletteIndex]);
  }

  if (currentMode == MUSIC_MODE && musicPaletteIndex == DARK_PALETTE_INDEX) {
    display.print("BassThr: ");  display.println(BASS_GATE_THRESH);
    display.print("TreblThr: "); display.println(TREBLE_GATE_THRESH);
  }

  // --- New: clarify arrow behavior on Home ---
  display.print("Arrows: ");
  if (ui == UI_HOME) {
    display.println("Palette");
  } else {
    display.println(ARROW_MODE_NAMES[arrowMode]);
  }

  if (arrowMode == ARROW_FLASH_COLOR) {
  display.print("Flash: ");
  display.println(FLASH_SETS[flashSetIdx].name);
  }

  if (arrowMode == ARROW_STROBE_COLOR) {
  display.print("Strobe: ");
  display.println(STROBE_SETS[strobeSetIdx].name);
  }

  display.display();
}



// wrap distance on circular strip
inline float wrapDistF(float a, float b, float n){
  float d = fabsf(a - b);
  return (d <= n - d) ? d : (n - d);
}

// smooth edge (0 outside → 1 inside) with soft shoulders
inline float softStep(float x, float halfLen, float edge){
  // x = distance from center
  float inner = fmaxf(0.0f, halfLen - edge);
  if (x >= halfLen) return 0.0f;
  if (x <= inner)   return 1.0f;
  // between inner..halfLen, apply smoothstep falloff
  float t = (x - inner) / fmaxf(1e-6f, (halfLen - inner));
  // smootherstep
  return 1.0f - (t*t*(3.0f - 2.0f*t));
}

// advance + render a palette as clouds to one strip
void renderPaletteClouds(CRGB* led, bool reverseIndex, const CRGBPalette16& pal, uint8_t baseV,
                         Cloud* C)
{
  // time step
  uint32_t nowUs = micros();
  uint32_t dtUs  = nowUs - cloudsLastUs;
  if (dtUs > 300000) dtUs = 300000;
  float dt = dtUs / 1000000.0f;

  // animate clouds
  for (uint8_t i=0;i<CLOUD_COUNT;i++){
    // centers drift
    C[i].center += C[i].speed * dt;
    while (C[i].center < 0)          C[i].center += NUM_LEDS;
    while (C[i].center >= NUM_LEDS)  C[i].center -= NUM_LEDS;

    // subtle breathing (length modulation)
    float breath = 1.0f + CLOUD_BREATHE * sinf( (millis()*0.0015f) + (C[i].wobble*0.0003f) );
    C[i].length = fminf(CLOUD_MAX_LEN, fmaxf(CLOUD_MIN_LEN, C[i].length * breath));
  }
  cloudsLastUs = nowUs;

  // black baseline
  fill_solid(led, NUM_LEDS, CRGB::Black);

  // precompute a moving palette offset like your original flow
  static uint16_t t1=0,t2=0,t3=0; t1+=1; t2+=2; t3+=3;

  for (int i=0;i<NUM_LEDS;i++){
    // palette index like before (keeps the same “feel”)
    uint8_t idx1 = sin8(i * 2 + (t1 >> 2));
    uint8_t idx2 = sin8(i * 3 + (t2 >> 3));
    uint8_t idx3 = sin8(i * 1 + (t3 >> 4));
    uint8_t colorIndex = (idx1/3) + (idx2/3) + (idx3/3);

    // find max contribution from any cloud (soft masks add by max)
    float m = 0.0f;
    for (uint8_t k=0;k<CLOUD_COUNT;k++){
      float d = wrapDistF((float)i, C[k].center, (float)NUM_LEDS);
      float halfLen = C[k].length * 0.5f;
      float w = softStep(d, halfLen, CLOUD_EDGE);
      if (w > m) m = w;
    }
    if (m <= 0.001f) continue; // remains black outside clouds

    // apply mask to brightness (soft edges)
    uint8_t V = (uint8_t)constrain((int)(baseV * m), 0, 255);
    // second strip uses an offset like you had
    uint8_t ci = reverseIndex ? colorIndex + 64 : colorIndex;
    led[ reverseIndex ? (NUM_LEDS-1-i) : i ] = ColorFromPalette(pal, ci, V);
  }
}

// ============== Palette blending render (MUSIC_MODE) ==============
void fx_paletteFlow() {
  static uint16_t t1 = 0, t2 = 0, t3 = 0;
  t1 += 1; t2 += 2; t3 += 3;

// NEW
uint8_t bassN   = sens(bandNorm[1]);
uint8_t trebleN = sens(bandNorm[5]);
uint8_t peakN   = sens(audioPeakN);

  // Gates (normalized) — compute ONCE
  const uint8_t BASS_GATE_N   = gateToNorm255(BASS_GATE_THRESH);
  const uint8_t TREBLE_GATE_N = gateToNorm255(TREBLE_GATE_THRESH);

  // Scene level (for background feel)
  static float sceneLevel = 0.0f;
  float target = peakN / 255.0f;
  float alpha  = 0.10f + 0.15f * target;
  sceneLevel   = (1.0f - alpha) * sceneLevel + alpha * target;

  auto can_hit = [](unsigned long lastMs, uint16_t debounce)->bool{
    return (millis() - lastMs) > debounce;
  };

  // ===== Laser strobe gate (same) =====
  unsigned long now = millis();
  if (normTo900(peakN) >= LASER_GATE_THRESH) {
    if (!laserStrobeActive && (now - lastLaserTrigger > LASER_DEBOUNCE_MS)) {
      laserStrobeActive = true;
      laserStrobeStart  = now;
      lastLaserTrigger  = now;
    }
  }

  // ---------- PALETTE BLEND ----------
  if (blending) {
    unsigned long e = millis() - blendStartTime;
    if (e >= blendDuration) { currentPal = targetPal; blending = false; }
    else {
      float k = (float)e / (float)blendDuration;
      for (int i = 0; i < 16; i++) {
        currentPal[i].r = startPal[i].r + (targetPal[i].r - startPal[i].r) * k;
        currentPal[i].g = startPal[i].g + (targetPal[i].g - startPal[i].g) * k;
        currentPal[i].b = startPal[i].b + (targetPal[i].b - startPal[i].b) * k;
      }
    }
  }

  // ============== DARK palette =========
  if (musicPaletteIndex == DARK_PALETTE_INDEX) {
    fill_solid(leds1, NUM_LEDS, CRGB::Black);
    fill_solid(leds2, NUM_LEDS, CRGB::Black);

    unsigned long nowMs = millis();
    if (bassN >= BASS_GATE_N || trebleN >= TREBLE_GATE_N) {
      musicGateOpenUntil = nowMs + MUSIC_GATE_HOLD;
    }
    laserAutoState = (nowMs < musicGateOpenUntil);

    // Energy-scaled pops (brightness + length)
    if (bassN >= BASS_GATE_N && can_hit(lastBassHitMs, BASS_HIT_DEBOUNCE)) {
      uint8_t vMax = hitV_u8(bassN, BASS_GATE_N);
      int     len  = scaledLen_u8(bassN, BASS_GATE_N, BASS_SEG_LEN, 32);
      spawnSegmentStrong(musicBassCursor, len, true, vMax);
      musicBassCursor = (musicBassCursor + BASS_STEP) % NUM_LEDS;
      lastBassHitMs = nowMs;
    }
    if (trebleN >= TREBLE_GATE_N && can_hit(lastTrebleHitMs, TREB_HIT_DEBOUNCE)) {
      uint8_t vMax = hitV_u8(trebleN, TREBLE_GATE_N);
      int     len  = scaledLen_u8(trebleN, TREBLE_GATE_N, TREB_SEG_LEN, 18);
      spawnSegmentStrong(musicTrebleCursor - len + 1, len, false, vMax);
      musicTrebleCursor = (musicTrebleCursor - TREBLE_STEP + NUM_LEDS) % NUM_LEDS;
      lastTrebleHitMs = nowMs;
    }

    addSegmentOverlay();
    return;
  }

  // -------- CLOUD / BLOCK RENDERING (non-Dark) ----------
  float curved = powf(constrain(sceneLevel, 0.f, 1.f), 0.8f);
  uint8_t bright = (uint8_t)(18 + (210 - 18) * curved);

  float motion = 0.6f + 1.0f * sceneLevel; // 0.6x → 1.6x
  float s1[CLOUD_COUNT], s2[CLOUD_COUNT];
  for (uint8_t i=0;i<CLOUD_COUNT;i++){ s1[i]=clouds1[i].speed; s2[i]=clouds2[i].speed;
                                       clouds1[i].speed*=motion;  clouds2[i].speed*=motion; }
  renderPaletteClouds(leds1, false, currentPal, bright, clouds1);
  renderPaletteClouds(leds2, true,  currentPal, bright, clouds2);
  for (uint8_t i=0;i<CLOUD_COUNT;i++){ clouds1[i].speed=s1[i]; clouds2[i].speed=s2[i]; }

  // Subtle shimmer
  uint8_t warpAmt = (uint8_t)(10 + 40 * sceneLevel);
  if (warpAmt > 0) {
    uint32_t t = millis();
    for (int i=0;i<NUM_LEDS;i++){
      if ((i + t/20) % 40 == 0) {
        uint8_t idx = (i*2 + (t>>4)) & 0xFF;
        CRGB w = ColorFromPalette(currentPal, idx, warpAmt);
        nblend(leds1[i], w, warpAmt);
        nblend(leds2[NUM_LEDS-1-i], w, warpAmt);
      }
    }
  }

  // Treble sparkles (kept)
  int trebleVal = normTo900(trebleN);
  uint8_t sparkleCeil = (uint8_t)(12 * SPARKLE_INTENSITY / 100);
  uint8_t sparkleProb = (uint8_t)constrain(map(trebleVal, 200, 900, 0, sparkleCeil), 0, sparkleCeil);
  if (sparkleProb > 0 && random8() < sparkleProb) {
    int p = random16(NUM_LEDS);
    CRGB sp = CRGB::White; sp.fadeLightBy(200);
    nblend(leds1[p], sp, 96);
    nblend(leds2[NUM_LEDS-1-p], sp, 96);
  }

  // Quiet fade smoothing
  if (sceneLevel < 0.15f) {
    uint8_t fade = (uint8_t)map((int)(sceneLevel*1000), 0, 150, 20, 8);
    fadeToBlackBy(leds1, NUM_LEDS, fade);
    fadeToBlackBy(leds2, NUM_LEDS, fade);
  }

  // ----------------- POP SEGMENTS (non-Dark) ----------------------
  unsigned long nowMs2 = millis();
  if (bassN >= BASS_GATE_N && can_hit(lastBassHitMs, BASS_HIT_DEBOUNCE)) {
    uint8_t vMax = hitV_u8(bassN, BASS_GATE_N);
    int     len  = scaledLen_u8(bassN, BASS_GATE_N, BASS_SEG_LEN, 28);
    spawnSegmentStrong(musicBassCursor, len, true, vMax);
    musicBassCursor = (musicBassCursor + BASS_STEP) % NUM_LEDS;
    lastBassHitMs = nowMs2;
  }
  if (trebleN >= TREBLE_GATE_N && can_hit(lastTrebleHitMs, TREB_HIT_DEBOUNCE)) {
    uint8_t vMax = hitV_u8(trebleN, TREBLE_GATE_N);
    int     len  = scaledLen_u8(trebleN, TREBLE_GATE_N, TREB_SEG_LEN, 16);
    spawnSegmentStrong(musicTrebleCursor - len + 1, len, false, vMax);
    musicTrebleCursor = (musicTrebleCursor - TREBLE_STEP + NUM_LEDS) % NUM_LEDS;
    lastTrebleHitMs = nowMs2;
  }
  addSegmentOverlay();
}






// Background flow (time-based, non-audio)
void renderSegmentBackground() {
  static uint16_t t1 = 0, t2 = 0, t3 = 0;
  t1 += 2; t2 += 3; t3 += 4; // a bit quicker than music mode

  // Blend currentPal if needed
  if (blending) {
    unsigned long now = millis();
    unsigned long elapsed = now - blendStartTime;
    if (elapsed >= blendDuration) {
      currentPal = targetPal;
      blending = false;
    } else {
      float progress = (float)elapsed / (float)blendDuration;
      for (int i = 0; i < 16; i++) {
        currentPal[i].r = startPal[i].r + (targetPal[i].r - startPal[i].r) * progress;
        currentPal[i].g = startPal[i].g + (targetPal[i].g - startPal[i].g) * progress;
        currentPal[i].b = startPal[i].b + (targetPal[i].b - startPal[i].b) * progress;
      }
    }
  }

  for (int i = 0; i < NUM_LEDS; i++) {
    uint8_t idx1 = sin8(i * 3 + (t1 >> 2));
    uint8_t idx2 = sin8(i * 2 + (t2 >> 3));
    uint8_t idx3 = sin8(i * 1 + (t3 >> 4));
    uint8_t colorIndex = (idx1 / 3) + (idx2 / 3) + (idx3 / 3);
    uint8_t baseV = BRIGHTNESS; // fixed V; pot controls global brightness
    leds1[i] = ColorFromPalette(currentPal, colorIndex, baseV);
    leds2[NUM_LEDS - 1 - i] = ColorFromPalette(currentPal, colorIndex + 64, baseV);
  }
}

inline int wrapDist(int a, int b) {
  // circular shortest distance on 0..NUM_LEDS-1
  int d = abs(a - b);
  return min(d, NUM_LEDS - d);
}

void addRippleOverlay() {
  advanceFlicker(); // still use flicker to add motion to the mix amount

  unsigned long now = millis();
  for (uint8_t k = 0; k < MAX_RIPPLES; k++) {
    if (!ripples[k].active) continue;
    uint32_t age = now - ripples[k].startMs;
    if (age > RIPPLE_FADE_MS) { ripples[k].active = false; continue; }

    float radius = age * RIPPLE_SPEED_PX_PER_MS;
    uint8_t life = 255 - map(age, 0, RIPPLE_FADE_MS, 0, 255);

    // pick accent per palette + type
    const CRGB accent = ripples[k].bass ? BASS_ACCENT[musicPaletteIndex]
                                        : TREBLE_ACCENT[musicPaletteIndex];

    // slight hue bias keeps ripples distinct for bass/treble
    uint8_t bias = ripples[k].bass ? 0 : 96;

    for (int i = 0; i < NUM_LEDS; i++) {
      int d = wrapDist(i, ripples[k].center);
      int band = abs(d - (int)radius);
      if (band <= RIPPLE_WIDTH) {
        // ring brightness with sharper center
        uint8_t ringV = scale8(255 - (band * (255 / (RIPPLE_WIDTH + 1))), life);

        // palette index wiggle
        uint8_t palIdx = ((i * 3) + (age >> 2) + bias + (flickerSeed & 0x1F)) & 0xFF;

        // base from currentPal
        CRGB base = ColorFromPalette(currentPal, palIdx, ringV);

        // jittered blend amount (ripples = lighter blend than segments)
        uint8_t mixAmt = 96 + ((i * 13 + flickerSeed * 71 + (age >> 3)) & 0x3F); // ~96..159

        CRGB c = blendToward(base, accent, mixAmt);

        mixOverlay(leds1[i], c, 224);
        mixOverlay(leds2[NUM_LEDS - 1 - i], c, 224);
      }
    }
  }
}

void addSegmentOverlay() {
  // No flicker needed; pops are deterministic and punchy
  unsigned long now = millis();

  for (uint8_t s = 0; s < MAX_SEGMENTS; s++) {
    if (!segments[s].active) continue;

    uint32_t age = now - segments[s].startMs;
    if (age > (POP_FLASH_MS + POP_HOLD_MS + POP_FADE_MS)) {
      segments[s].active = false;
      continue;
    }

    // Choose accent per palette & type
    const CRGB accent = segments[s].bass ? BASS_ACCENT[musicPaletteIndex]
                                         : TREBLE_ACCENT[musicPaletteIndex];
// phases:
bool flashPhase = age < POP_FLASH_MS_K;
bool holdPhase  = (!flashPhase) && (age < POP_FLASH_MS_K + POP_HOLD_MS_K);
bool fadePhase  = (!flashPhase && !holdPhase);

// fade amount:
  uint8_t fadeV = 255;                         // <-- add this line
if (fadePhase) {
  uint32_t fAge = age - (POP_FLASH_MS_K + POP_HOLD_MS_K);
  fadeV = 255 - map((uint16_t)min<uint32_t>(fAge, POP_FADE_MS_K), 0, POP_FADE_MS_K, 0, 255);
}


    // Segment bounds
    // --- replace the inner loop from "int segLen = ..." down to the writes ---
int segLen = max(0, segments[s].length);

// precompute lane bias & dark flag once
uint8_t laneBias = segments[s].bass ? 24 : 160;
bool darkSelected = (musicPaletteIndex == DARK_PALETTE_INDEX);

for (int o = 0; o < segLen; o++) {
  int p1 = (segments[s].start + o) % NUM_LEDS;
  int p2 = (NUM_LEDS - 1) - p1;

  // texture/jitter drives palette index
  uint8_t jitter = ((p1 * 7) + (age >> 2)) & 0x1F;
  uint8_t palIdx1 = ((p1 * 2) + laneBias + jitter) & 0xFF;
  uint8_t palIdx2 = ((p2 * 2) + laneBias + jitter) & 0xFF; // separate index for strip2

  // base colors: in Dark, choose by HIT TYPE (bass vs treble), not by strip
CRGB base1, base2;
if (darkSelected) {
  const CRGBPalette16& hitPal = segments[s].bass ? PALETTE_DARK_BASS
                                                 : PALETTE_DARK_TREBLE;
  // same color on both strips (mirrored)
  CRGB base = ColorFromPalette(hitPal, palIdx1, 255);
  base1 = base;
  base2 = base;
} else {
  const CRGB accent = segments[s].bass ? BASS_ACCENT[musicPaletteIndex]
                                       : TREBLE_ACCENT[musicPaletteIndex];
  base1 = base2 = accent;
}


  // apply the pop envelope
  uint8_t vMax = segments[s].vMax;               // 180..255 per hit
uint8_t fadeScaled = scale8_video(fadeV, vMax); // fade shaped by max

auto applyEnvelope = [&](CRGB c)->CRGB {
  if (flashPhase) {
    // White push also scales with vMax (big hits = whiter flash)
    uint8_t whiteAmt = scale8_video(200, vMax); // up to ~200
    nblend(c, CRGB::White, whiteAmt);
    return c;
  }
  if (holdPhase) {
    c.nscale8_video(vMax); // hold at per-hit max
    return c;
  }
  c.nscale8_video(fadeScaled); // fade down from per-hit max
  return c;
};


  CRGB pop1 = applyEnvelope(base1);
  CRGB pop2 = applyEnvelope(base2);

  // optional white edge tips
  if (POP_EDGE_WHITE && (flashPhase || holdPhase) && (o == 0 || o == segLen - 1)) {
    pop1 = CRGB::White;
    pop2 = CRGB::White;
  }

  // write: overwrite on flash/hold; blend on fade
  if (flashPhase || holdPhase) {
    leds1[p1] = pop1;
    leds2[p2] = pop2;
  } else {
    nblend(leds1[p1], pop1, 200);
    nblend(leds2[p2], pop2, 200);
  }
}
  }
}




// Replace your spawnRipple / spawnSegment with these:

void spawnRipple(int center, bool isBass) {
  // try free slot first
  for (uint8_t k = 0; k < MAX_RIPPLES; k++) {
    if (!ripples[k].active) {
      ripples[k] = { center, millis(), isBass, true };
      return;
    }
  }
  // overwrite the OLDEST
  uint8_t oldest = 0;
  unsigned long oldestAge = 0;
  unsigned long now = millis();
  for (uint8_t k = 0; k < MAX_RIPPLES; k++) {
    unsigned long age = now - ripples[k].startMs;
    if (age > oldestAge) { oldestAge = age; oldest = k; }
  }
  ripples[oldest] = { center, now, isBass, true };
}

void spawnSegment(int start, int len, bool isBass) {
  int normStart = (start % NUM_LEDS + NUM_LEDS) % NUM_LEDS;

  for (uint8_t s = 0; s < MAX_SEGMENTS; s++) {
    if (!segments[s].active) {
      segments[s] = { normStart, len, millis(), isBass, true, /*vMax*/ 220 };
      return;
    }
  }
  uint8_t oldest = 0; unsigned long oldestAge = 0; unsigned long now = millis();
  for (uint8_t s = 0; s < MAX_SEGMENTS; s++) {
    unsigned long age = now - segments[s].startMs;
    if (age > oldestAge) { oldestAge = age; oldest = s; }
  }
  segments[oldest] = { normStart, len, now, isBass, true, /*vMax*/ 220 };
}


// NEW helper: strength-aware spawn
void spawnSegmentStrong(int start, int len, bool isBass, uint8_t vMax) {
  int normStart = (start % NUM_LEDS + NUM_LEDS) % NUM_LEDS;

  // try free slot first
  for (uint8_t s = 0; s < MAX_SEGMENTS; s++) {
    if (!segments[s].active) {
      segments[s] = { normStart, len, millis(), isBass, true, vMax };
      return;
    }
  }
  // overwrite the oldest
  uint8_t oldest = 0; unsigned long oldestAge = 0, now = millis();
  for (uint8_t s = 0; s < MAX_SEGMENTS; s++) {
    unsigned long age = now - segments[s].startMs;
    if (age > oldestAge) { oldestAge = age; oldest = s; }
  }
  segments[oldest] = { normStart, len, now, isBass, true, vMax };
}


void fx_segmentDJ() {
  // --- Blend currentPal if needed (so 1–9 cross-fade in DJ mode too) ---
  if (blending) {
    unsigned long now = millis();
    unsigned long elapsed = now - blendStartTime;
    if (elapsed >= blendDuration) {
      currentPal = targetPal;
      blending = false;
    } else {
      float progress = (float)elapsed / (float)blendDuration;
      for (int i = 0; i < 16; i++) {
        currentPal[i].r = startPal[i].r + (targetPal[i].r - startPal[i].r) * progress;
        currentPal[i].g = startPal[i].g + (targetPal[i].g - startPal[i].g) * progress;
        currentPal[i].b = startPal[i].b + (targetPal[i].b - startPal[i].b) * progress;
      }
    }
  }

  // --- Cloud/block baseline (non-audio) ---
  // Use fixed base brightness; your pot still sets global FastLED brightness.
  const uint8_t baseV = BRIGHTNESS;
  renderPaletteClouds(leds1, /*reverseIndex=*/false, currentPal, baseV, clouds1);
  renderPaletteClouds(leds2, /*reverseIndex=*/true,  currentPal, baseV, clouds2);

  // --- Overlays from active segments (pop envelope) ---
  addSegmentOverlay();
  // If you want the ripples too, uncomment:
  addRippleOverlay();
}


// ============== INPUT / KEY HANDLING ==============
void setMusicPalette(uint8_t idx) {
  if (idx >= MUSIC_PALETTE_COUNT) return;
  startPal       = currentPal;
  targetPal      = musicPalettes[idx];
  blendStartTime = millis();
  blending       = true;
  musicPaletteIndex = idx;

  // If leaving Dark, drop auto laser drive immediately
  if (musicPaletteIndex != DARK_PALETTE_INDEX) {
    laserAutoState = false;
    musicGateOpenUntil = 0;
  }

  Serial.print("Palette -> ");
  Serial.println(musicPaletteNames[musicPaletteIndex]);
  updateDisplay();
}


void handleInputs() {
  while (Serial.available() > 0) {
    char c = Serial.read();

    // ===== Arrow-key state machine (ESC [ A/B/C/D) =====
    if (escState == ESC_IDLE) {
      if ((uint8_t)c == 0x1B) {  // ESC
        escState = ESC_SEEN;
        continue;
      }
    } else if (escState == ESC_SEEN) {
      if (c == '[') { escState = ESC_BRACKET; continue; }
      else { escState = ESC_IDLE; /* fall through */ }
    } else if (escState == ESC_BRACKET) {
      if (c == 'A' || c == 'B' || c == 'C' || c == 'D') {
        // Right/Left switch target among the arrow modes (now includes FlashColor & Brightness)
if (c == 'C' || c == 'D') {
  const uint8_t FIRST = ARROW_STROBE_COLOR;     
  const uint8_t LAST  = ARROW_MODE_COUNT - 1; // ARROW_BRIGHTNESS
  if (c == 'C') { // Right
    arrowMode = (arrowMode >= LAST) ? FIRST : (arrowMode + 1);
  } else {        // Left
    arrowMode = (arrowMode <= FIRST) ? LAST : (arrowMode - 1);
  }
  Serial.print("Arrows -> ");
  Serial.println(ARROW_MODE_NAMES[arrowMode]);
  updateDisplay();
  escState = ESC_IDLE;
  continue;
}


        // Up/Down adjust the current gate
        bool up = (c == 'A');
        switch (arrowMode) {
          case ARROW_MUSIC_GATE:
            MUSIC_GATE_THRESH  = constrain(MUSIC_GATE_THRESH  + (up ? +ARROW_STEP_THRESH : -ARROW_STEP_THRESH), 0, 900);
            Serial.printf("MusicGate=%d\n",  MUSIC_GATE_THRESH);
            break;
          case ARROW_BASS_GATE:
            BASS_GATE_THRESH   = constrain(BASS_GATE_THRESH   + (up ? +ARROW_STEP_THRESH : -ARROW_STEP_THRESH),  0, 900);
            Serial.printf("BassGate=%d\n",   BASS_GATE_THRESH);
            break;
          case ARROW_TREBLE_GATE:
            TREBLE_GATE_THRESH = constrain(TREBLE_GATE_THRESH + (up ? +ARROW_STEP_THRESH : -ARROW_STEP_THRESH), 0, 900);
            Serial.printf("TrebleGate=%d\n", TREBLE_GATE_THRESH);
            break;
          case ARROW_LASER_GATE:   // <-- NEW
            LASER_GATE_THRESH  = constrain(LASER_GATE_THRESH  + (up ? +ARROW_STEP_THRESH : -ARROW_STEP_THRESH), 0, 900);
            Serial.printf("LaserGate=%d\n",  LASER_GATE_THRESH);
            break;
          case ARROW_FLASH_COLOR: {
          // Up = previous set, Down = next set (wrap)
          int next = (int)flashSetIdx + (up ? -1 : +1);
          if (next < 0) next = FLASH_SET_COUNT - 1;
          if (next >= FLASH_SET_COUNT) next = 0;
          flashSetIdx = (uint8_t)next;
          Serial.printf("FlashColor -> %s (idx=%u)\n", FLASH_SETS[flashSetIdx].name, flashSetIdx);
          break;
              }
          case ARROW_BRIGHTNESS: {
          // Up/Down nudge global brightness
          uint8_t cur = FastLED.getBrightness();
          int v = (int)cur + (up ? +ARROW_STEP_BRIGHT : -ARROW_STEP_BRIGHT);
          v = constrain(v, BRIGHT_MIN, BRIGHT_MAX);
          FastLED.setBrightness((uint8_t)v);
          Serial.printf("Brightness=%d\n", v);
          break;
              }
          case ARROW_STROBE_COLOR: {
         // Up = previous set, Down = next set (wrap)
          int next = (int)strobeSetIdx + (up ? -1 : +1);
          if (next < 0) next = STROBE_SET_COUNT - 1;
          if (next >= STROBE_SET_COUNT) next = 0;
          strobeSetIdx = (uint8_t)next;
          Serial.printf("StrobeColor -> %s (idx=%u)\n", STROBE_SETS[strobeSetIdx].name, strobeSetIdx);
          break;
          }

          default:
            // ignore other modes while pots/buttons are disabled
            break;
        }
        updateDisplay();
        escState = ESC_IDLE;
        continue;
      } else {
        escState = ESC_IDLE; // abort sequence
      }
    }

    // ================================================

    // ---- Auto palette cycle toggle (Music mode) ----
    if (c == 'a' || c == 'A') {
      if (currentMode != MUSIC_MODE) currentMode = MUSIC_MODE;
      autoCyclePal     = !autoCyclePal;
      nextPaletteCycle = millis() + PALETTE_CYCLE_MS;
      Serial.printf("Auto palette cycle %s (every %lus)\n",
                    autoCyclePal ? "ON" : "OFF", PALETTE_CYCLE_MS/1000);
      updateDisplay();
      continue;
    }

    if (c == 'Z') { dumpIOOnce(); continue; }

    // ---- Blackout 'd' ----
    if (c == 'd') {
      blackoutActive = true;
      Serial.println("Blackout: fade to black (press any key to resume)");
      continue;
    }
    if (blackoutActive) {
      blackoutActive = false;
      Serial.println("Blackout cleared");
      // fall through to handle this key normally
    }

    // ---- Keyboard strobe 's' (toggle) ----
    if (c == 's' || c == 'S') {
      strobeFromKey = !strobeFromKey;
      Serial.printf("Strobe (keyboard) %s\n", strobeFromKey ? "ON" : "OFF");
      continue;
    }

    // ---- Palette keys (1..9) ----
    if (c >= '1' && c <= '9') {
      uint8_t pidx = (uint8_t)(c - '1'); // 0..8
      if (pidx < MUSIC_PALETTE_COUNT) {
        if (currentMode == FX_MODE &&
            (currentEffect == FX_SEGMENT_DJ || currentEffect == FX_BOUNCE)) {
          setMusicPalette(pidx);
        } else {
          currentMode = MUSIC_MODE;
          setMusicPalette(pidx);
        }
      }
      continue;
    }

    if (c == 'g' || c == 'G') {
      debugBands = !debugBands;
      Serial.printf("MSGEQ7 serial debug %s\n", debugBands ? "ON" : "OFF");
      if (debugBands) { printBandsLine(); printBandsBars(); }
      return;
    }

    // ---- Explicit FX keys ----
    if (c == 'q' || c == 'Q') { currentMode = FX_MODE; currentEffect = FX_CONFETTI;  Serial.println("Effect -> Confetti"); updateDisplay(); continue; }
    if (c == 'w' || c == 'W') { currentMode = FX_MODE; currentEffect = FX_BOUNCE;    Serial.println("Effect -> Bounce");   updateDisplay(); continue; }
    if (c == 'r' || c == 'R') { currentMode = FX_MODE; currentEffect = FX_RAINBOW;   Serial.println("Effect -> Rainbow");  updateDisplay(); continue; }
    if (c == 'e' || c == 'E') { currentMode = FX_MODE; currentEffect = FX_SEGMENT_DJ;Serial.println("Effect -> DJ Segments"); updateDisplay(); continue; }

    // ---- Live taps only when DJ Segments is active ----
    if (currentMode == FX_MODE && currentEffect == FX_SEGMENT_DJ) {
      if (c == 'n' || c == 'N') { spawnSegment(bassCursor, BASS_SEG_LEN, true);  bassCursor  = (bassCursor  + BASS_STEP) % NUM_LEDS; continue; }
      if (c == 'c' || c == 'C') { spawnSegment(trebleCursor - TREB_SEG_LEN + 1, TREB_SEG_LEN, false); trebleCursor = (trebleCursor - TREBLE_STEP + NUM_LEDS) % NUM_LEDS; continue; }
    }

    // ---- Momentary flash ----
    if (c == 'f' || c == 'F') { flashPulseUntil = millis() + FLASH_PULSE_MS; continue; }

    // ---- FX next/prev ----
    if (c == 'P') { currentEffect = (currentEffect + FX_COUNT - 1) % FX_COUNT; currentMode = FX_MODE; updateDisplay(); continue; }
    if (c == 'N') { currentEffect = (currentEffect + 1) % FX_COUNT;            currentMode = FX_MODE; updateDisplay(); continue; }

    // ---- Bounce jolt ----
    if ((c == 'k' || c == 'K') && currentMode == FX_MODE && currentEffect == FX_BOUNCE) {
      uint32_t now = millis();
      auto stack_to = [&](uint32_t &deadline){
        uint32_t leftover = (deadline > now) ? (deadline - now) : 0;
        deadline = now + min<uint32_t>(1400, leftover + BOUNCE_JOLT_MS);
      };
      stack_to(joltUntilMs1);
      stack_to(joltUntilMs2);
      int h1 = (int)(b1Pos256 >> 8), h2 = (int)(b2Pos256 >> 8);
      spawnStaticPulse(true,  h1, true);  spawnStaticPulse(true,  h1, false);
      spawnStaticPulse(false, h2, false); spawnStaticPulse(false, h2, true);
      Serial.println("Bounce JOLT + OUTWARD STATIC!");
      continue;
    }

    // ---- Back to Music from DJ/Bounce ----
    if ((c == 'm' || c == 'M') &&
        currentMode == FX_MODE &&
        (currentEffect == FX_SEGMENT_DJ || currentEffect == FX_BOUNCE)) {
      currentMode = MUSIC_MODE; Serial.println("Mode -> Music (keeping current palette)"); updateDisplay(); continue;
    }

    // ---- Direct gate nudges (b/B, t/T) ----
    if (c == 'b') { BASS_GATE_THRESH   = max(0,   BASS_GATE_THRESH   - 10); Serial.print("BassThresh=");   Serial.println(BASS_GATE_THRESH);   continue; }
    if (c == 'B') { BASS_GATE_THRESH   = min(900, BASS_GATE_THRESH   + 10); Serial.print("BassThresh=");   Serial.println(BASS_GATE_THRESH);   continue; }
    if (c == 't') { TREBLE_GATE_THRESH = max(0,   TREBLE_GATE_THRESH - 10); Serial.print("TrebleThresh="); Serial.println(TREBLE_GATE_THRESH); continue; }
    if (c == 'T') { TREBLE_GATE_THRESH = min(900, TREBLE_GATE_THRESH + 10); Serial.print("TrebleThresh="); Serial.println(TREBLE_GATE_THRESH); continue; }

    // ---- Lasers ----
    if (c == 'l' || c == 'L') {
      unsigned long now = millis();
      if (c == 'L') {
        if (!laserStrobeActive && (now - lastLaserTrigger > LASER_DEBOUNCE_MS)) {
          laserStrobeActive = true; laserStrobeStart = now; lastLaserTrigger = now;
          Serial.println("Laser STROBE burst"); updateDisplay();
        }
        continue;
      }
      laserOn = !laserOn; Serial.printf("Laser %s\n", laserOn ? "ON" : "OFF"); updateDisplay(); continue;
    }

    // ---- Rotate arrow target: 'p' (cycle Music → Bass → Treble → Laser) ----
if (c == 'p' || c == 'P') {
  if      (arrowMode == ARROW_MUSIC_GATE)   arrowMode = ARROW_BASS_GATE;
  else if (arrowMode == ARROW_BASS_GATE)    arrowMode = ARROW_TREBLE_GATE;
  else if (arrowMode == ARROW_TREBLE_GATE)  arrowMode = ARROW_LASER_GATE;   // <-- NEW hop
  else                                      arrowMode = ARROW_MUSIC_GATE;   // wrap to start
  Serial.print("Arrows -> ");
  Serial.println(ARROW_MODE_NAMES[arrowMode]);
  updateDisplay();
  continue;
}

    // ---- Restore defaults: 'o' ----
    if (c == 'o' || c == 'O') {
      flashSetIdx        = DEFAULT_FLASHSET;
      MUSIC_GATE_THRESH  = DEFAULT_MUSIC_GATE;
      BASS_GATE_THRESH   = DEFAULT_BASS_GATE;
      TREBLE_GATE_THRESH = DEFAULT_TREBLE_GATE;
      FastLED.setBrightness(DEFAULT_BRIGHTNESS);
      Serial.println("Restored defaults: flash color, gates, brightness.");
      updateDisplay();
      continue;
    }
  }
}



// Renders active "segments[]" using PartyColors_p with flash/hold/fade envelope.
// Baseline is black — only the segments are shown.
void addPartySegmentOverlay_MusicDark() {
  unsigned long now = millis();
  for (uint8_t s = 0; s < MAX_SEGMENTS; s++) {
    if (!segments[s].active) continue;

    uint32_t age = now - segments[s].startMs;
    if (age > (POP_FLASH_MS + POP_HOLD_MS + POP_FADE_MS)) {
      segments[s].active = false;
      continue;
    }

    bool flashPhase = age < POP_FLASH_MS;
    bool holdPhase  = (!flashPhase) && (age < POP_FLASH_MS + POP_HOLD_MS);
    bool fadePhase  = (!flashPhase && !holdPhase);

    uint8_t fadeV = 255;
    if (fadePhase) {
      uint32_t fAge = age - (POP_FLASH_MS + POP_HOLD_MS);
      fadeV = 255 - map((uint16_t)min<uint32_t>(fAge, POP_FADE_MS), 0, POP_FADE_MS, 0, 255);
    }

    // Different lane per bass/treble so repeated hits don’t look identical
    uint8_t laneBias = segments[s].bass ? 24 : 160;

    int segLen = max(0, segments[s].length);
    for (int o = 0; o < segLen; o++) {
  int p1 = (segments[s].start + o) % NUM_LEDS;
  int p2 = (NUM_LEDS - 1) - p1;

  // Palette index with mild texture; separate per strip for variety
  uint8_t jitter   = ((p1 * 7) + (age >> 2)) & 0x1F;
  uint8_t palIdx1  = ((p1 * 2) + jitter) & 0xFF;       // strip 1
  uint8_t palIdx2  = ((p2 * 2) + jitter + 64) & 0xFF;  // strip 2, offset

  // --- PICK COLOR BY STRIP (not by segment type) ---
  CRGB base1 = ColorFromPalette(PALETTE_DARK_BASS,   palIdx1, 255);  // strip 1 = Indigo
  CRGB base2 = ColorFromPalette(PALETTE_DARK_TREBLE, palIdx2, 255);  // strip 2 = Red

  // Envelope
  auto applyEnv = [&](CRGB c)->CRGB {
    if (flashPhase) { nblend(c, CRGB::White, 80); return c; }
    if (holdPhase)  { return c; }
    c.nscale8_video(fadeV);
    return c;
  };

  CRGB c1 = applyEnv(base1);
  CRGB c2 = applyEnv(base2);

  // Optional white tips while flashing/holding
  if (POP_EDGE_WHITE && (flashPhase || holdPhase) && (o == 0 || o == segLen - 1)) {
    c1 = CRGB::White;
    c2 = CRGB::White;
  }

  // Write: overwrite on flash/hold; blend on fade
  if (flashPhase || holdPhase) {
    leds1[p1] = c1;
    leds2[p2] = c2;
  } else {
    nblend(leds1[p1], c1, 200);
    nblend(leds2[p2], c2, 200);
  }
}

  }
}

void spawnStaticPulse(bool onStrip1, int headIdx, bool dirRight) {
  StaticPulse* arr = onStrip1 ? pulses1 : pulses2;
  // find free slot
  for (uint8_t i = 0; i < MAX_PULSES; i++) {
    if (!arr[i].active) { arr[i] = { headIdx, dirRight, millis(), true }; return; }
  }
  // overwrite oldest
  uint8_t oldest = 0; uint32_t oldestAge = 0, now = millis();
  for (uint8_t i = 0; i < MAX_PULSES; i++) {
    uint32_t age = now - arr[i].startMs;
    if (age > oldestAge) { oldestAge = age; oldest = i; }
  }
  arr[oldest] = { headIdx, dirRight, now, true };
}

void renderStaticPulses(StaticPulse* arr, CRGB* strip) {
  uint32_t now = millis();
  for (uint8_t i = 0; i < MAX_PULSES; i++) {
    if (!arr[i].active) continue;
    uint32_t age = now - arr[i].startMs;
    if (age > STATIC_PULSE_MS) { arr[i].active = false; continue; }

    // center of traveling window
    int32_t distPx = (int32_t)((int64_t)STATIC_PULSE_PPS * age / 1000); // integer, px
    int center = arr[i].dirRight ? (arr[i].startIdx + distPx)
                                 : (arr[i].startIdx - distPx);

    int start = center - (STATIC_PULSE_LEN / 2);
    int end   = center + (STATIC_PULSE_LEN / 2);

    // fade the whole window out over time
    uint8_t life = 255 - map(age, 0, STATIC_PULSE_MS, 0, 255);

    for (int p = start; p <= end; p++) {
  if (p < 0 || p >= NUM_LEDS) continue;

  // Perlin noise base
  uint8_t n = inoise8(p * 11, age * 8);

  // Derive a sparsity factor from your existing knobs:
  // - lower STATIC_INTENSITY -> fewer specks
  // - shorter windows -> fewer specks (keeps total specks reasonable)
  // sparseBase in [1..8] (1 = dense, 8 = very sparse)
  uint8_t sparseBase = 1 + (uint8_t)((255 - STATIC_INTENSITY) / 36);     // 0..7 -> +1
  uint8_t lenBias    = 1 + (uint8_t)max(0, (int)(STATIC_PULSE_LEN - 12)) / 36;
  uint8_t sparseMod  = (uint8_t)min<uint16_t>(8, sparseBase + lenBias);  // clamp 1..8

  // Random-ish gate using a cheap hash — hits when mod == 0
  uint8_t rnd = (uint8_t)((uint16_t)p * 131 + (age * 17));
  bool speck = (rnd % sparseMod) == 0;

  // Base brightness from noise and lifetime
  uint8_t v = scale8(n, STATIC_INTENSITY);
  v = scale8(v, life);

  // Palette-tinted snow, with occasional brighter specks
  CRGB c = ColorFromPalette(currentPal, (p * 3 + age) & 0xFF, v);
  if (speck) nblend(c, CRGB::White, 96); // tasteful pop, not full white

  nblend(strip[p], c, v);
}
  }
}

// --- A: Palettes/Flash ---
int blendMsDefault = 250;
Param A_Palette    = {"Palette", &musicPaletteIndex, Param::P_U8, 0, (int)MUSIC_PALETTE_COUNT-1, 0, 0, 1, 1};
Param A_BlendMs    = {"BlendMs", &blendDuration,     Param::P_INT, 60, 4000, 250, 0, 100, 10};
Param A_FlashSet   = {"FlashSet",&flashSetIdx,       Param::P_U8, 0, (int)FLASH_SET_COUNT-1, 0, 0, 1, 1};

// --- B: Gates ---
Param B_MusicGate  = {"MusicGate",  &MUSIC_GATE_THRESH,  Param::P_INT, 0, 900, DEFAULT_MUSIC_GATE, 0, 10, 2};
Param B_BassGate   = {"BassGate",   &BASS_GATE_THRESH,   Param::P_INT, 0, 900, DEFAULT_BASS_GATE,  0, 10, 2};
Param B_TrebleGate = {"TrebleGate", &TREBLE_GATE_THRESH, Param::P_INT, 0, 900, DEFAULT_TREBLE_GATE,0, 10, 2};
Param B_Sensitivity = { "Sensitivity%", &GATE_SENS_Q8, Param::P_U8, 40, 255, 170, 0, 5, 1 };

// --- C: FX/Music feel ---
Param C_BouncePps  = {"BouncePPS",  (void*)&BOUNCE_PPS,   Param::P_INT, 2, 120, 10, 0, 2, 1};
Param C_BounceLen  = {"BounceLen",  (void*)&BOUNCE_LEN,   Param::P_INT, 10, 200, 50, 0, 2, 1};
Param C_JoltPps    = {"JoltPPS",    (void*)&BOUNCE_JOLT_PPS, Param::P_INT, 20, 240, 60, 0, 5, 2};

// These three are “virtual” knobs that scale existing behaviors:
uint8_t CLOUD_EDGE_SOFT_KNOB = 50;      // maps to CLOUD_EDGE/softness factor
uint8_t CLOUD_SPEED_SCALE    = 100;     // 100% = unchanged
uint8_t SPARKLE_INTENSITY    = 50;      // 0..100 → maps to prob cap
Param C_CloudSpeed = {"CloudSpeed%", &CLOUD_SPEED_SCALE,   Param::P_U8, 30, 200, 100, 0, 5, 1};
Param C_CloudEdge  = {"CloudEdge%",  &CLOUD_EDGE_SOFT_KNOB,Param::P_U8, 10, 200,  100, 0, 5, 1};
Param C_Sparkle    = {"Sparkle%",    &SPARKLE_INTENSITY,   Param::P_U8,  0, 100,   40, 0, 5, 1};

// --- D: Overlays/Strobe/Lasers ---
uint16_t POP_FLASH_MS_K = POP_FLASH_MS, POP_HOLD_MS_K = POP_HOLD_MS, POP_FADE_MS_K = POP_FADE_MS;
Param D_PopMacro  = {"PopMacro",  &POP_FLASH_MS_K,  Param::P_INT, 20, 160, POP_FLASH_MS, 0, 5, 1}; // macro will set all three
Param D_StrobeSpd = {"StrobeMs", (void*)&TOUCH_STROBE_SPEED,
                     Param::P_U8, 20, 200, TOUCH_STROBE_SPEED, 0, 5, 1};
Param D_LaserGate = {"LaserGate", &LASER_GATE_THRESH, Param::P_INT, 100, 900, LASER_GATE_THRESH, 0, 10, 2};


static void initButtons() {
  const uint8_t pins[8] = {BTN_A,BTN_B,BTN_C,BTN_D,BTN_E,BTN_F,BTN_G,BTN_H};
  for (int i=0;i<8;i++){
    bool hasPullup = !(pins[i]==34 || pins[i]==35 || pins[i]==36 || pins[i]==39);
    pinMode(pins[i], hasPullup ? INPUT_PULLUP : INPUT);
    BTN[i].pin = pins[i];
    BTN[i].lastLevel = btnIdleLevel();
    BTN[i].lastChangeMs = 0;
    BTN[i].pressed = false;
    BTN[i].fellEdge = BTN[i].roseEdge = false;
  }
}


static void scanButtons() {
  const uint32_t now = millis();
  for (int i = 0; i < 8; i++) {
    bool lvl = digitalRead(BTN[i].pin);
    BTN[i].fellEdge = BTN[i].roseEdge = false;

    if (lvl != BTN[i].lastLevel) {
      BTN[i].lastLevel = lvl;
      BTN[i].lastChangeMs = now;
    }

    // simple debounce (6–12 ms is usually fine)
    if (now - BTN[i].lastChangeMs > 12) {
      bool pressed = BTN_ACTIVE_LOW ? (lvl == LOW) : (lvl == HIGH);
      if (pressed != BTN[i].pressed) {
        BTN[i].pressed = pressed;
        if (pressed) BTN[i].fellEdge = true; else BTN[i].roseEdge = true;
      }
    }
  }
}


int readPotFiltered(uint8_t pin, int &lastRaw, int dead) {
  int raw = analogRead(pin);
  if (lastRaw < 0) { lastRaw = raw; return raw; }
  if (abs(raw - lastRaw) < dead) return lastRaw;
  lastRaw = raw;
  return raw;
}

void applyParamDelta(const Param* p, bool fine, int deltaSign) {
  int step = fine ? p->stepFine : p->stepCoarse;

  if (p->type == Param::P_INT) {
    int v = *((int*)p->ptr);
    v = constrain(v + deltaSign * step, p->iMin, p->iMax);
    *((int*)p->ptr) = v;
     if (p == &A_Palette) {
      setMusicPalette((uint8_t)v);
    }
  } else if (p->type == Param::P_U8) {
    int v = *((uint8_t*)p->ptr);
    v = constrain(v + deltaSign * step, p->iMin, p->iMax);
    *((uint8_t*)p->ptr) = (uint8_t)v;
     if (p == &A_Palette) {
      setMusicPalette((uint8_t)v);
    }
  } else {
    float v = *((float*)p->ptr);
    float fstep = (float)step;
    v = fminf(p->fMax, fmaxf(p->fMin, v + deltaSign * fstep));
    *((float*)p->ptr) = v;
     if (p == &A_Palette) {
      setMusicPalette((uint8_t)v);
    }
  }
}


// ==== QUICK I/O MONITOR (press 'Z') ====
// ==== QUICK I/O MONITOR (press 'Z') ====
void dumpIOOnce() {
  int raw = analogRead(POT1_PIN);
  int ba = digitalRead(BTN_A), bb = digitalRead(BTN_B),
      bc = digitalRead(BTN_C), bd = digitalRead(BTN_D);

  Serial.printf("\nADC: POT(32)=%4d\n", raw);
  Serial.printf("BTN: A(17)=%s  B(19)=%s  C(4)=%s  D(23)=%s  (LOW=pressed)\n",
                ba?"HIGH":"LOW", bb?"HIGH":"LOW", bc?"HIGH":"LOW", bd?"HIGH":"LOW");
}


static void drawHome() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0,0);
  display.println("Visualizer");

  display.print("Mode: ");
  display.println(currentMode == MUSIC_MODE ? "Music" : "FX");

  if (currentMode == MUSIC_MODE) {
    display.println("Effect: PaletteFlow");
  } else {
    display.print("Effect: ");
    display.println(FX[currentEffect].name);
  }

  display.println();
display.println("E/G:Palette  H:Settings");
display.println("A:Laser  B/C/D:FX");

display.print("Knobs: ");
display.println(potMode == PM_BRIGHT_MUSIC ? "Bright+Music" : "Bass+Treble");
display.print("Gate M/B/T: ");
display.print(MUSIC_GATE_THRESH); display.print("/");
display.print(BASS_GATE_THRESH);  display.print("/");
display.println(TREBLE_GATE_THRESH);
display.print("Sens%: ");
display.println((unsigned)((uint16_t)GATE_SENS_Q8*100/255));


  display.print("Palette: ");
  display.println(musicPaletteNames[musicPaletteIndex]);

  display.print("Laser: ");
  display.println(laserOn ? "ON" : "OFF");

  display.println();
  display.println("H:Settings  A:Laser  B/C/D:FX");
  display.print("Knobs: ");
display.println(potMode == PM_BRIGHT_MUSIC ? "Bright+Music" : "Bass+Treble");
  display.display();

}

static void drawSettingsRoot() {
  const char* items[SI_COUNT] = {"Music", "Palette Color", "Flash Color", "Knob Mode"};

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0,0);
  display.println("Settings");

  for (uint8_t i=0;i<SI_COUNT;i++){
    if (i==menuCursor) display.print("> "); else display.print("  ");
    if (i == SI_KNOBMODE) {
      display.print(items[i]); display.print(": ");
      display.println(potMode == PM_BRIGHT_MUSIC ? "Bright+Music" : "Bass+Treble");
    } else {
      display.println(items[i]);
    }
  }
  display.setCursor(0,56);
  display.print("E/G:Up/Down  F:Enter  H:Back");
  display.display();
}


static void drawSettingsMusic() {
  const char* items[4] = { "Music Gate", "Bass Gate", "Treble Gate", "Sensitivity%" };
  int valsInt[3] = {MUSIC_GATE_THRESH, BASS_GATE_THRESH, TREBLE_GATE_THRESH};

  // Convert sensitivity to percent for display (approx)
  int sensPct = (int)((uint16_t)GATE_SENS_Q8 * 100 / 255);

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0,0);
  display.println("Music Settings");

  // rows 0..2: the three gates
  for (uint8_t i = 0; i < 3; i++) {
    if (i==musicCursor) display.print("> "); else display.print("  ");
    display.print(items[i]); display.print(": ");
    display.println(valsInt[i]);
  }

  // row 3: Sensitivity%
  if (3==musicCursor) display.print("> "); else display.print("  ");
  display.print(items[3]); display.print(": ");
  display.print(sensPct); display.println("%");

  display.setCursor(0,56);
  display.print("E/G:Select  F:Adjust  H:Back");
  display.display();
}


static void drawParamAdjust(const char* name, const char* hint, const char* valueText) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0,0);
  display.println(name);
  display.println("---------------------");
  display.println(valueText);
  display.setCursor(0,56);
  display.print(hint); // "Turn knob, F:Apply, H:Back"
  display.display();
}

static void enterParamAdjust(ParamTarget pt) {
  activeParam = pt;
  ui = UI_PARAM_ADJUST;

  potPickupLocked = true;
  potEntryRaw     = readAdjPot();
  potLastCommitRaw= potEntryRaw;

  uiLastActivityMs = millis();

  // Initial draw
  char value[32];
    switch (pt) {
    case PT_MUSIC_GATE: snprintf(value,sizeof(value),"MusicGate=%d", MUSIC_GATE_THRESH); break;
    case PT_BASS:       snprintf(value,sizeof(value),"BassGate=%d",  BASS_GATE_THRESH);  break;
    case PT_TREBLE:     snprintf(value,sizeof(value),"TrebleGate=%d",TREBLE_GATE_THRESH);break;
    case PT_PALETTE:    snprintf(value,sizeof(value),"Palette=%s",   musicPaletteNames[musicPaletteIndex]); break;
    case PT_FLASHSET:   snprintf(value,sizeof(value),"Flash=%s",     FLASH_SETS[flashSetIdx].name); break;

    case PT_SENSITIVITY: {
      char pct[8];
      snprintf(pct, sizeof(pct), "%u%%", (unsigned)((uint16_t)GATE_SENS_Q8*100/255));
      drawParamAdjust("Sensitivity", "Turn knob, F:Apply, H:Back", pct);
      return; // <-- important: skip the generic draw below
    }

    default:            snprintf(value,sizeof(value),"-"); break;
  }

  // Generic draw for non-sensitivity params:
  drawParamAdjust(
    (pt==PT_MUSIC_GATE||pt==PT_BASS||pt==PT_TREBLE) ? "Music Param" :
    (pt==PT_PALETTE ? "Palette Color" : "Flash Color"),
    "Turn knob, F:Apply, H:Back",
    value
  );
}

static void exitToSettingsMenu() {
  // Exit adjust → back to the right menu level
  if (activeParam==PT_MUSIC_GATE || activeParam==PT_BASS || activeParam==PT_TREBLE) {
    ui = UI_SETTINGS_MUSIC;
    drawSettingsMusic();
  } else {
    ui = UI_SETTINGS;
    drawSettingsRoot();
  }
  activeParam = PT_NONE;
}

static void tickParamAdjust() {
  // buttons
  if (BTN[BI_H].fellEdge) { // back
    uiLastActivityMs = millis();
    exitToSettingsMenu();
    return;
  }
  if (BTN[BI_F].fellEdge) { // apply/commit (explicit)
    uiLastActivityMs = millis();
    // Nothing special to do because we “commit” whenever mapped value changes past pickup.
    // This press simply returns to menu.
    exitToSettingsMenu();
    return;
  }

  // POT: only change when we move past pickup threshold, then apply mapped value
  int raw = readAdjPot();
  if (potEntryRaw < 0) potEntryRaw = raw;

  bool canWrite = true;
  if (potPickupLocked) {
    if (abs(raw - potEntryRaw) >= POT_PICKUP_DEAD) {
      potPickupLocked = false;  // unlocked; next writes are allowed
    } else {
      canWrite = false;         // ignore jitter before pickup
    }
  }

  if (!canWrite) return;

  auto refreshHUD = [&](const char* name, const char* value){
    drawParamAdjust(name, "Turn knob, F:Apply, H:Back", value);
  };

  // Write only when mapped VALUE changes (prevents boundary chatter)
    char buf[32];

  if (activeParam == PT_MUSIC_GATE || activeParam == PT_BASS || activeParam == PT_TREBLE) {
    // map 0..4095 → 0..900
    int mapped = map(raw, 0, 4095, 0, 900);
    mapped = constrain(mapped, 0, 900);

    int* slot = nullptr;
    const char* nm = "Music Param";
    if (activeParam == PT_MUSIC_GATE) { slot = &MUSIC_GATE_THRESH;  nm = "Music Gate"; }
    if (activeParam == PT_BASS)       { slot = &BASS_GATE_THRESH;   nm = "Bass Gate";  }
    if (activeParam == PT_TREBLE)     { slot = &TREBLE_GATE_THRESH; nm = "Treble Gate";}

    if (slot && mapped != *slot) {
      *slot = mapped;
      uiLastActivityMs = millis();
      snprintf(buf, sizeof(buf), "%s=%d", nm, *slot);
      drawParamAdjust(nm, "Turn knob, F:Apply, H:Back", buf);
    }

  } else if (activeParam == PT_PALETTE) {
    int idx = map(raw, 0, 4095, 0, (int)MUSIC_PALETTE_COUNT - 1);
    idx = constrain(idx, 0, (int)MUSIC_PALETTE_COUNT - 1);
    if (idx != (int)musicPaletteIndex) {
      setMusicPalette((uint8_t)idx);
      uiLastActivityMs = millis();
      snprintf(buf,sizeof(buf),"Palette=%s", musicPaletteNames[musicPaletteIndex]);
      drawParamAdjust("Palette Color", "Turn knob, F:Apply, H:Back", buf);
    }

  } else if (activeParam == PT_FLASHSET) {
    int idx = map(raw, 0, 4095, 0, (int)FLASH_SET_COUNT - 1);
    idx = constrain(idx, 0, (int)FLASH_SET_COUNT - 1);
    if (idx != (int)flashSetIdx) {
      flashSetIdx = (uint8_t)idx;
      uiLastActivityMs = millis();
      snprintf(buf,sizeof(buf),"Flash=%s", FLASH_SETS[flashSetIdx].name);
      drawParamAdjust("Flash Color", "Turn knob, F:Apply, H:Back", buf);
    }

  } else if (activeParam == PT_SENSITIVITY) {
    // Map pot to 40..255  (≈16%..100%)
    uint8_t v = (uint8_t)constrain(map(raw, 0, 4095, 40, 255), 40, 255);
    if (v != GATE_SENS_Q8) {
      GATE_SENS_Q8 = v;
      uiLastActivityMs = millis();
      unsigned pct = (unsigned)((uint16_t)GATE_SENS_Q8 * 100 / 255);
      snprintf(buf, sizeof(buf), "Sensitivity=%u%%", pct);
      drawParamAdjust("Sensitivity", "Turn knob, F:Apply, H:Back", buf);
    }
  }
}


static void goHome() {
  ui = UI_HOME;
  uiLastActivityMs = millis();
  // reset pot pickup on entering Home
  potPickupRaw = -1;
  potPickup    = true;
  drawHome();
}


static void tickSettingsRoot() {
  if (BTN[BI_H].fellEdge) { goHome(); return; }

  if (BTN[BI_E].fellEdge) { if (menuCursor>0) menuCursor--; uiLastActivityMs=millis(); drawSettingsRoot(); }
  if (BTN[BI_G].fellEdge) { if (menuCursor<SI_COUNT-1) menuCursor++; uiLastActivityMs=millis(); drawSettingsRoot(); }

  if (BTN[BI_F].fellEdge) {
  uiLastActivityMs = millis();
  if (menuCursor == SI_MUSIC) {
    ui = UI_SETTINGS_MUSIC; musicCursor=0; drawSettingsMusic();
  } else if (menuCursor == SI_PALETTE) {
    enterParamAdjust(PT_PALETTE);
  } else if (menuCursor == SI_FLASH) {
    enterParamAdjust(PT_FLASHSET);
  } else if (menuCursor == SI_KNOBMODE) {
    potMode = (potMode == PM_BRIGHT_MUSIC) ? PM_BASS_TREBLE : PM_BRIGHT_MUSIC;
    // reset pickup so the new mapping doesn't jump
    potA_pickupLocked = potB_pickupLocked = true;
    potA_entryRaw = potB_entryRaw = -1;
    Serial.printf("KnobMode -> %s\n", potMode==PM_BRIGHT_MUSIC ? "Bright+Music" : "Bass+Treble");
    drawSettingsRoot();
  }
}
}

static void tickSettingsMusic() {
  if (BTN[BI_H].fellEdge) { ui = UI_SETTINGS; drawSettingsRoot(); return; }
  if (BTN[BI_E].fellEdge) { if (musicCursor>0) musicCursor--; uiLastActivityMs=millis(); drawSettingsMusic(); }
  if (BTN[BI_G].fellEdge) { if (musicCursor<3) musicCursor++; uiLastActivityMs=millis(); drawSettingsMusic(); }
  if (BTN[BI_F].fellEdge) {
  uiLastActivityMs = millis();
  if (musicCursor==0) enterParamAdjust(PT_MUSIC_GATE);
  if (musicCursor==1) enterParamAdjust(PT_BASS);
  if (musicCursor==2) enterParamAdjust(PT_TREBLE);
  if (musicCursor==3) enterParamAdjust(PT_SENSITIVITY);  // NEW
}
}


static void tickHome() {
  // H single press → Settings
  if (BTN[BI_H].fellEdge) { ui = UI_SETTINGS; menuCursor=0; uiLastActivityMs=millis(); drawSettingsRoot(); return; }

  // H hold (>700ms) → panic back to Music mode
  static uint32_t hPressStart = 0;
  if (BTN[BI_H].fellEdge)   hPressStart = millis();
  if (BTN[BI_H].pressed && (millis() - hPressStart) > 700) {
    currentMode = MUSIC_MODE;
    uiLastActivityMs = millis();
    drawHome();
  }

  // --- NEW: On Home, E/G cycle palette ---
  if (BTN[BI_E].fellEdge || BTN[BI_G].fellEdge) {
    int delta = BTN[BI_E].fellEdge ? -1 : +1;                 // E = Up/prev, G = Down/next
    int next  = (int)musicPaletteIndex + delta;
    if (next < 0) next = MUSIC_PALETTE_COUNT - 1;
    if (next >= MUSIC_PALETTE_COUNT) next = 0;
    setMusicPalette((uint8_t)next);                           // smooth blend
    uiLastActivityMs = millis();
    drawHome();                                               // refresh HUD
    return;
  }

  // Laser toggle (A)
  if (BTN[BI_A].fellEdge) {
    laserOn = !laserOn;
    updateDisplay();
    drawHome();
    uiLastActivityMs = millis();
  }

  // FX selects (B/C/D)
  if (BTN[BI_B].fellEdge) { currentMode = FX_MODE; currentEffect = FX_CONFETTI;  updateDisplay(); drawHome(); uiLastActivityMs=millis(); }
  if (BTN[BI_C].fellEdge) { currentMode = FX_MODE; currentEffect = FX_BOUNCE;    updateDisplay(); drawHome(); uiLastActivityMs=millis(); }
  if (BTN[BI_D].fellEdge) { currentMode = FX_MODE; currentEffect = FX_SEGMENT_DJ;updateDisplay(); drawHome(); uiLastActivityMs=millis(); }

  // F enters FX tweak when in FX mode
  if (BTN[BI_F].fellEdge && currentMode == FX_MODE) {
    ui = UI_FX_TWEAK;
    uiLastActivityMs = millis();
    potPickupRaw = -1; potPickup = true;
    display.clearDisplay();
    display.setTextSize(1); display.setTextColor(SSD1306_WHITE); display.setCursor(0,0);
    display.println("FX Tweak");
    display.print("Effect: "); display.println(FX[currentEffect].name);
    if (currentEffect == FX_BOUNCE)
      display.println("Pot: Length  (F toggles Speed)");
    else if (currentEffect == FX_CONFETTI)
      display.println("Pot: Brightness");
    display.println("H:Back");
    display.display();
  }
}

static void tickFxTweak() {
  // Back to Home
  if (BTN[BI_H].fellEdge) { goHome(); return; }

  // Bounce: F cycles pot target (Length <-> Speed)
  if (currentEffect == FX_BOUNCE && BTN[BI_F].fellEdge) {
    bouncePotTargetsLen = !bouncePotTargetsLen;
    uiLastActivityMs = millis();

    // refresh HUD
    display.clearDisplay();
    display.setTextSize(1); display.setTextColor(SSD1306_WHITE); display.setCursor(0,0);
    display.println("FX Tweak");
    display.print("Effect: "); display.println(FX[currentEffect].name);
    display.print("Pot: "); display.println(bouncePotTargetsLen ? "Length" : "Speed");
    display.println("H:Back");
    display.display();

    Serial.printf("Bounce pot -> %s\n", bouncePotTargetsLen ? "Length" : "Speed");
  }
}


// CALL THIS ONCE in setup()
void initNewUI() {
  initButtons();
  uiLastActivityMs = millis();
  goHome();
}

// CALL THIS EACH FRAME in loop()
void uiTick() {
  scanButtons();


  // 6s inactivity (no button edges & no committed pot changes)
  if ((millis() - uiLastActivityMs) > UI_IDLE_MS && ui != UI_HOME) {
    goHome();
    return;
  }

switch (ui) {
  case UI_HOME:           tickHome(); break;
  case UI_SETTINGS:       tickSettingsRoot(); break;
  case UI_SETTINGS_MUSIC: tickSettingsMusic(); break;
  case UI_PARAM_ADJUST:   tickParamAdjust(); break;
  case UI_FX_TWEAK:       tickFxTweak(); break;
}
}
