
/*
  Firmware: .... presence-aware-switch
  Version: ..... 1.1.0
  Hardware: .... ESP-32
  Author: ...... Scott Griffis
  Date: ........ 06/15/2025
  
  Description:
    This firmware is intended for an Expressif ESP-32 device. It allows for the 
    device to control an outlet based on presence. It does this by allowing a
    Bluetooth LE beacon to be paired with the device. When the beacon is within 
    range then the associated outlet is powered on. When the beacon is missing
    then the accociated outlet is powered off.
*/

#include <Arduino.h>
#include <ArduinoBLE.h>
#include <Settings.h>
#include <map>
#include <vector>

#define PAIR_PIN 32
#define LEARN_LED_PIN 9
#define CONTROLLED_DEVICE_PIN 2

#define INIT_ON_STATE false

Settings settings;

// Function Prototypes
// --------------------------------------
void doLearnTask();
void doBTScan();
void doPurgeOldSeenDevices();
void doHandleOnOffSwitching();
void doDeterminePairedDeviceProximity();

std::map<String, ulong> seenDevices;
std::map<String, int> seenRssis;
std::vector<String> purgeList;

/**
 * SETUP
 * =====
 * The main setup portion of the firmware.
 * 
 */
void setup() {
  pinMode(PAIR_PIN, INPUT);
  pinMode(CONTROLLED_DEVICE_PIN, OUTPUT);

  settings.loadSettings();

  digitalWrite(CONTROLLED_DEVICE_PIN, settings.isOnState() ? HIGH : LOW);

  // Initialize Serial for Output
  Serial.begin(9600);
  if (!Serial) ESP.restart();

  // Initialize Bluetooth
  Serial.print("Starting Bluetooth... ");
  if (!BLE.begin()) {
    Serial.println("Issue with starting Bluetooth!");
    delay(5000L);
    ESP.restart();
  }
  Serial.println("Complete.");
  BLE.scan();
}

/**
 * MAIN LOOP
 * =========
 * The main looping part of the firmware.
 * 
 */
void loop() {
  doBTScan();
  doLearnTask();
  doHandleOnOffSwitching();
  
  yield();
}

/**
 * Used to update the controlledOnState so that it reflects the current
 * proximity of the paired device.
 * 
 */
void doDeterminePairedDeviceProximity() {
  bool sState = settings.isOnState();
  settings.setOnState(seenDevices.count(settings.getParedAddress()) > 0);
  if (sState != settings.isOnState()) {
    settings.saveSettings();
  }
}

/**
 * This function handles the on/off switching of the controlled device
 * such that the state of the device is made to match the desired state of the
 * device.
 * 
 */
void doHandleOnOffSwitching() {
  doDeterminePairedDeviceProximity();

  if (settings.isOnState() && digitalRead(CONTROLLED_DEVICE_PIN) == LOW) {
    // Device is off but should be on; Turn it on
    digitalWrite(CONTROLLED_DEVICE_PIN, HIGH);
    Serial.printf("Device: ON!!!\n");
  } else if (!settings.isOnState() && digitalRead(CONTROLLED_DEVICE_PIN) == HIGH) {
    // Device is on but should be off; Turn it off
    digitalWrite(CONTROLLED_DEVICE_PIN, LOW);
    Serial.printf("Device: OFF!!!\n");
  }
}

/**
 * Used to perge old seen devices which are no longer considered
 * to be in-range.
 * 
 */
void doPurgeOldSeenDevices() {
  // Purge seenDevices that are too old
  int devCount = seenDevices.size();
  String deleteList[devCount] = {""};

  // Locate old devices which need purged
  for (const auto& pair : seenDevices) {
    if (millis() - pair.second > settings.getMaxNotSeenMillis()) {
      purgeList.push_back(pair.first);
    }
  }

  // Purge the identified devices
  for (String id : purgeList) {
    seenDevices.erase(id);
    seenRssis.erase(id);
    Serial.printf("Purged 'seen' device; device=[%s]\n", id.c_str());
  }
  purgeList.clear();
}

/**
 * Handles doing the Bluetooth Scan routine for identifying
 * nearby devices also initiates the purging of old devices
 * which had been seen last too long ago.
 * 
 */
void doBTScan() {
  doPurgeOldSeenDevices();

  // Add or update seen device to map
  BLEDevice dev = BLE.available();
  if (dev && dev.rssi() > settings.getMaxNearRssi()) {
    // Seen device is not out of range
    Serial.printf("Found new near device; device=[%s]; rssid=[%d]\n", dev.address().c_str(), dev.rssi());
    seenDevices[dev.address()] = millis();
    seenRssis[dev.address()] = dev.rssi();
  }
}

/**
 * Handles the learning task. The learning task allows
 * for the device to identify and track the device which is nearest 
 * to it at the time the learning is performed.
 * 
 */
void doLearnTask() {
  static ulong highStartMillis = 0L;
  static ulong learnStartMillis = 0L;
  static bool overHold = false;
  static bool learning = false;
  static bool learnStarted = false;

  if (learning) {
    // Do start of learning tasks
    if (!learnStarted) {
      // TODO: LED ON...
      learnStartMillis = millis();
      Serial.printf("Learning started...\n");
      learnStarted = true;
    }

    // Wait 10 Seconds to allow nearest discovery then pair with nearest
    if (millis() - learnStartMillis > settings.getLearnWaitMillis()) {
      String nearestId = "";
      int nearestRssi = -999;

      // Check known devices for nearest
      for (const auto& pair : seenDevices) {
        if (nearestId.isEmpty() || seenRssis[pair.first] > nearestRssi) {
          nearestId = pair.first;
          nearestRssi = seenRssis[pair.first];
        }
      }
      // Pair with identified ID
      if (!settings.getParedAddress().equalsIgnoreCase(nearestId)) {
        settings.setParedAddress(nearestId);
        settings.saveSettings();
        Serial.printf("Learning Complete! Paired Device is '%s', with RSSI of: %d\n\n", nearestId.c_str(), nearestRssi);
      } else {
        Serial.printf("Learning Complete! Paired Device is same as previous!\n\n");
      }

      // Do end of learning tasks
      learning = false;
      learnStarted = false;
      
      // TODO: LED OFF...
    }
  } else {
    // Not currently learning so check to see if learn button is pressed
    if (!overHold && digitalRead(PAIR_PIN) == HIGH) {
      // Learning button is pressed
      if (highStartMillis == 0) {
        // Start the button press length timer
        highStartMillis = millis();
      } else if (millis() - highStartMillis > settings.getEnableLearnHoldMillis()) {
        // Enter learning mode if button pressed for more than 5 seconds
        learning = true;
        overHold = true;
      }
    } else if (digitalRead(PAIR_PIN) == LOW) {
      // Clear learning button timer and all other related states
      highStartMillis = 0L;
      overHold = false;
    }
  }
}