# Pulse OS - ESP32 Firmware

Pulse OS is the hardware-side firmware for the **Pulse Bridge** system. It transforms an ESP32 into a smart external dashboard that displays real-time media metadata, phone status, and supports touchless gesture controls via an IR sensor.

## 🛠 Hardware Configuration
* **Microcontroller:** ESP32 (WROOM/S3)
* **Display:** 16x2 Character LCD (Parallel interface)
    * *Pins:* RS:4, E:5, D4:26, D5:25, D6:33, D7:32
* **Sensor:** HW-201 IR Obstacle Sensor
    * *Pin:* 18 (Active LOW)
* **Communication:** Bluetooth Low Energy (BLE)

## 📡 The Pulse Protocol
The firmware hosts a GATT Server with a custom Service UUID. It handles two-way communication:
1.  **Metadata (Rx):** Receives a piped string `Title|Artist|Battery|Signature` from the Android app.
2.  **Control (Tx):** Notifies the Android app of user gestures (e.g., "TOGGLE" for play/pause).

## Technical Evolution & Quirks
Building this was an iterative process that required solving several real-world hardware challenges:

### 1. The "Ghost Toggle" Fix (Gesture Debouncing)
* **The Problem:** The IR sensor was too sensitive. A single hand wave would trigger multiple play/pause commands, causing the phone to interpret it as a "Next Track" command (double-tap logic).
* **The Fix:** I implemented a **1200ms Software Lockout**. After a gesture is detected, the IR sensor is "blinded" for 1.2 seconds, ensuring only one clean command is sent to the phone per wave. We also added a **50ms noise floor** to ignore electrical flickers.

### 2. Robust Pipe Parsing
* **The Problem:** Initial parsing was fragile. If the artist name or signature was missing, the string slicing would fail, causing the LCD to show blank lines or old data.
* **The Fix:** I implemented a **"Greedy" Piped Parser**. It explicitly searches for the positions of all three `|` symbols and handles substrings gracefully. If a field is empty, the system retains the last known good value or displays a fallback.

### 3. State-Driven LCD Refreshing
* **The Problem:** Constantly clearing the LCD caused noticeable flickering, while not clearing it left "ghost" characters behind when a long song title was replaced by a shorter one.
* **The Fix:** I introduced a `metaUpdated` flag. The LCD only performs a full `clear()` when the BLE callback detects a genuine change in the song title or artist, ensuring a smooth, flicker-free UI.

### 4. BLE vs. Classic Bluetooth
* **The Problem:** The device wouldn't appear in the phone's system Bluetooth settings.
* **The Fix:** Optimized the advertising parameters to work with the companion Android app's specific Service UUID (`4fafc201-1fb5-459e-8fcc-c5c9c331914b`). This allows for an instant, seamless connection within the app without manual system pairing.

## Interaction Map
* **Short Swipe (< 500ms):** Sends a `TOGGLE` command to the phone to Play/Pause music.
* **Long Hold (> 1000ms):** Switches the ESP32 display between **Now Playing** mode and the **Dashboard** (Battery/Signature).
* **Automatic Scrolling:** If a song title or artist is longer than 16 characters, the display automatically initiates a marquee scroll.

## Setup & Installation
1.  **Library Dependencies:** * `LiquidCrystal`
    * `BLEDevice` (ESP32 Built-in)
2.  **Baud Rate:** Ensure Serial Monitor is set to `115200`.
3.  **Partition Scheme:** If the code fails to upload, change the Partition Scheme to **"Huge APP (3MB No OTA)"** in the Arduino IDE Tools menu, as the BLE stack is resource-intensive.

---
*Developed as part of the Pulse Bridge Ecosystem.*
