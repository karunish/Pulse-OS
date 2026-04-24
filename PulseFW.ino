// ============================================================
//  PULSE OS - ESP32 Firmware
//  LCD: RS=4, E=5, D4=26, D5=25, D6=33, D7=32
//  IR Sensor (HW-201): Pin 18
// ============================================================

#include <Arduino.h>
#include <LiquidCrystal.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// ── LCD ─────────────────────────────────────────────────────
//  LiquidCrystal(RS, E, D4, D5, D6, D7)
LiquidCrystal lcd(4, 5, 26, 25, 33, 32);

// ── IR Sensor ───────────────────────────────────────────────
#define IR_PIN 18
// HW-201 outputs LOW when object is detected

// ── BLE UUIDs ───────────────────────────────────────────────
#define SERVICE_UUID       "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define METADATA_CHAR_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define CONTROL_CHAR_UUID  "beb5483e-36e1-4688-b7f5-ea07361b26a9"

// ── BLE Globals ─────────────────────────────────────────────
BLEServer*         pServer      = nullptr;
BLECharacteristic* pControlChar = nullptr;
bool               deviceConnected = false;

// ── Screen State ────────────────────────────────────────────
enum Screen { SCREEN_NOW_PLAYING, SCREEN_DASHBOARD };
Screen currentScreen = SCREEN_NOW_PLAYING;

// ── Metadata Strings (updated by BLE writes) ────────────────
String songTitle = "Pulse  OS";
String artist    = "Waiting...";
String battLevel = "--";
String signalStr = "--";

// ── Marquee / Scrolling ─────────────────────────────────────
unsigned long lastScrollTime  = 0;
int           scrollTitleIdx  = 0;
int           scrollArtistIdx = 0;
const int     SCROLL_DELAY_MS = 380;   // ms between scroll steps

// ── Display Refresh ─────────────────────────────────────────
unsigned long lastDisplayTime = 0;
const int     DISPLAY_INTERVAL = 150; // refresh rate in ms

// ── IR Gesture Timing ───────────────────────────────────────
unsigned long irPressStart = 0;
bool          irWasBlocked = false;

// ============================================================
//  HELPER: Scrolling marquee — returns a 16-char window
// ============================================================
String marquee(const String& text, int pos, int width = 16) {
  int len = text.length();
  if (len <= width) {
    // Pad to width so LCD doesn't show stale chars
    String padded = text;
    while ((int)padded.length() < width) padded += ' ';
    return padded;
  }
  // Add a spacer between repeats for clean looping
  String src = text + "    ";
  int srcLen  = src.length();
  String out  = "";
  for (int i = 0; i < width; i++) {
    out += src[(pos + i) % srcLen];
  }
  return out;
}

// ============================================================
//  HELPER: Pad or truncate string to exact LCD width
// ============================================================
String lcdLine(const String& s, int width = 16) {
  String out = s;
  if ((int)out.length() > width) out = out.substring(0, width);
  while ((int)out.length() < width) out += ' ';
  return out;
}

// ============================================================
//  DISPLAY RENDER
// ============================================================
void renderDisplay() {
  if (currentScreen == SCREEN_NOW_PLAYING) {
    lcd.setCursor(0, 0);
    lcd.print(marquee(songTitle, scrollTitleIdx));
    lcd.setCursor(0, 1);
    lcd.print(marquee(artist, scrollArtistIdx));
  } else {
    // Dashboard: We use lcdLine to ensure we overwrite old scrolling text
    lcd.setCursor(0, 0);
    String bLine = "Batt: " + battLevel;
    if (battLevel != "--") bLine += "%";
    lcd.print(lcdLine(bLine));
    
    lcd.setCursor(0, 1);
    lcd.print(lcdLine(signalStr));
  }
}

// ============================================================
//  BLE SERVER CALLBACKS
// ============================================================
class MyServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* s) override {
    deviceConnected = true;
    Serial.println("[BLE] Phone connected.");
    // Show connected indicator briefly
    lcd.setCursor(0, 0);
    lcd.print(lcdLine("Phone Connected!"));
    delay(800);
  }
  void onDisconnect(BLEServer* s) override {
    deviceConnected = false;
    Serial.println("[BLE] Disconnected. Restarting advertising...");
    BLEDevice::startAdvertising();
  }
};

// ============================================================
//  METADATA CHARACTERISTIC CALLBACK (WRITE from phone)
//  Format: TITLE|ARTIST|BATT%|SIGNAL
// ============================================================
volatile bool metaUpdated = false; // Flag to force LCD refresh

class MetadataCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* c) override {
    String val = c->getValue(); 
    if (val.length() == 0) return;
    val.trim(); // Remove hidden \n or spaces

    Serial.println("[BLE] Received: " + val);

    // Find all pipe positions
    int p1 = val.indexOf('|');
    int p2 = (p1 != -1) ? val.indexOf('|', p1 + 1) : -1;
    int p3 = (p2 != -1) ? val.indexOf('|', p2 + 1) : -1;

    // Robust Slicing
    String newTitle  = (p1 != -1) ? val.substring(0, p1) : val;
    String newArtist = (p1 != -1 && p2 != -1) ? val.substring(p1 + 1, p2) : artist;
    String newBatt   = (p2 != -1 && p3 != -1) ? val.substring(p2 + 1, p3) : battLevel;
    String newSignal = (p3 != -1) ? val.substring(p3 + 1) : signalStr;

    // Update global state
    if (newTitle != songTitle || newArtist != artist) {
      scrollTitleIdx  = 0;
      scrollArtistIdx = 0;
      metaUpdated = true; 
    }

    songTitle = newTitle;
    artist    = newArtist;
    battLevel = newBatt;
    signalStr = newSignal;
  }
};
// ============================================================
//  SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  Serial.println("\n=============================");
  Serial.println("  PULSE OS - Starting Up");
  Serial.println("=============================");

  // ── LCD Init ──
  lcd.begin(16, 2);
  lcd.print("  PULSE  OS  ");
  lcd.setCursor(0, 1);
  lcd.print("  Booting...  ");

  // ── IR Sensor ──
  pinMode(IR_PIN, INPUT);

  // ── BLE Init ──
  BLEDevice::init("PulseOS");

  // Print MAC to Serial
  Serial.print("[BLE] MAC Address: ");
  Serial.println(BLEDevice::getAddress().toString().c_str());

  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  BLEService* pService = pServer->createService(SERVICE_UUID);

  // ── MetadataChar: WRITE (phone → ESP32) ──
  BLECharacteristic* pMetaChar = pService->createCharacteristic(
    METADATA_CHAR_UUID,
    BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR
  );
  pMetaChar->setCallbacks(new MetadataCallbacks());

  // ── ControlChar: NOTIFY (ESP32 → phone) ──
  pControlChar = pService->createCharacteristic(
    CONTROL_CHAR_UUID,
    BLECharacteristic::PROPERTY_NOTIFY
  );
  pControlChar->addDescriptor(new BLE2902());

  pService->start();

  // ── Advertising ──
  BLEAdvertising* pAdv = BLEDevice::getAdvertising();
  pAdv->addServiceUUID(SERVICE_UUID);
  pAdv->setScanResponse(true);
  pAdv->setMinPreferred(0x06);
  BLEDevice::startAdvertising();

  Serial.println("[BLE] Advertising. Waiting for phone...");

  delay(1200);
  lcd.clear();
}

// ============================================================
//  MAIN LOOP
// ============================================================
unsigned long lastGestureTime = 0;
const int GESTURE_LOCKOUT_MS = 1200; // Ignore IR for 1.2s after a trigger

void loop() {
  unsigned long now = millis();

  // ── IR GESTURE DETECTION ────────────────────────────────
  bool irBlocked = (digitalRead(IR_PIN) == LOW);

  // Only process if we aren't in the lockout period
  if (now - lastGestureTime > GESTURE_LOCKOUT_MS) {
    if (irBlocked && !irWasBlocked) {
      irPressStart = now;
      irWasBlocked = true;
    }

    if (!irBlocked && irWasBlocked) {
      irWasBlocked = false;
      unsigned long duration = now - irPressStart;

      if (duration > 50 && duration < 500) { // Added a 50ms floor to ignore noise
        Serial.println("[IR] Short swipe → TOGGLE");
        if (deviceConnected) {
          pControlChar->setValue("TOGGLE");
          pControlChar->notify();
          lastGestureTime = now; // START LOCKOUT
          
          lcd.setCursor(14, 0);
          lcd.print(">>"); 
        }
      } else if (duration >= 1000) {
        Serial.println("[IR] Long hold → Switch screen");
        currentScreen = (currentScreen == SCREEN_NOW_PLAYING) ? SCREEN_DASHBOARD : SCREEN_NOW_PLAYING;
        scrollTitleIdx = 0;
        scrollArtistIdx = 0;
        lastGestureTime = now; // START LOCKOUT
        lcd.clear(); 
      }
    }
  } else {
    // If we are in lockout, reset the 'wasBlocked' state so we don't 
    // accidentally trigger immediately after the lockout ends
    irWasBlocked = false;
  }

  // ── REFRESH LCD ON NEW DATA ──
  if (metaUpdated) {
    lcd.clear();
    metaUpdated = false;
  }

  // ── MARQUEE SCROLL TICK ──
  if (now - lastScrollTime > SCROLL_DELAY_MS) {
    lastScrollTime = now;
    if (currentScreen == SCREEN_NOW_PLAYING) {
      if ((int)songTitle.length() > 16) scrollTitleIdx = (scrollTitleIdx + 1) % (songTitle.length() + 4);
      if ((int)artist.length() > 16) scrollArtistIdx = (scrollArtistIdx + 1) % (artist.length() + 4);
    }
  }

  // ── DISPLAY REFRESH ──
  if (now - lastDisplayTime > DISPLAY_INTERVAL) {
    lastDisplayTime = now;
    renderDisplay();
  }
}