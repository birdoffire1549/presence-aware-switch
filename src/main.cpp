
/*
  Firmware: .... presence-aware-switch
  Version: ..... 1.2.0
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
#define LEARN_LED_PIN 13
#define CONTROLLED_DEVICE_PIN 2
#define CLOSE_LED_PIN 17

#define INIT_ON_STATE false

Settings settings;

// Function Prototypes
// --------------------------------------
void doCheckLearnTask();
void doBTScan();
void doPurgeOldSeenDevices();
void doHandleOnOffSwitching();
void doDeterminePairedDeviceProximity();
void doCheckForCloseDevice();
void doHandleButtonPresses();
void doCheckFactoryReset();

std::map<String, ulong> seenDevices;
std::map<String, int> seenRssis;
std::vector<String> purgeList;

bool triggerFactoryReset = false;
bool triggerDeviceLearn = false;

/**
 * SETUP
 * =====
 * The main setup portion of the firmware.
 * 
 */
void setup() {
  pinMode(PAIR_PIN, INPUT);
  pinMode(CONTROLLED_DEVICE_PIN, OUTPUT);
  pinMode(LEARN_LED_PIN, OUTPUT);
  pinMode(CLOSE_LED_PIN, OUTPUT);

  settings.loadSettings();

  digitalWrite(CONTROLLED_DEVICE_PIN, settings.isOnState() ? HIGH : LOW);
  digitalWrite(LEARN_LED_PIN, LOW);
  digitalWrite(CLOSE_LED_PIN, LOW);

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

  Serial.printf("Learn Hold: %d millis\n", settings.getEnableLearnHoldMillis());
  Serial.printf("Learn Wait: %d millis\n", settings.getLearnWaitMillis());
  Serial.printf("Max Not Seen: %d millis\n", settings.getMaxNotSeenMillis());
  Serial.printf("Max Near RSSI: %d \n", settings.getMaxNearRssi());
  Serial.printf("Paired Address: %s\n", settings.getParedAddress().c_str());

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
  doHandleOnOffSwitching();
  doCheckForCloseDevice();
  doHandleButtonPresses();
  doCheckFactoryReset();
  doCheckLearnTask();

  yield();
}

/**
 * This function is the sole handler of the learn button's 
 * functionality. It notifies other functions when various tasks
 * need to be performed using boolean event flags.
 */
void doHandleButtonPresses() {
  static ulong timerMillis = 0UL;
  if (digitalRead(PAIR_PIN) == HIGH) {
    if (timerMillis == 0UL) {
      timerMillis = millis();
    }
    ulong nowMillis = millis();
    if (nowMillis - timerMillis > 30000UL) {
      for (int i = 0; i < 4; i++) {
        digitalWrite(LEARN_LED_PIN, digitalRead(LEARN_LED_PIN) == HIGH ? LOW : HIGH);
        delay(50UL);
      }
    } else if (nowMillis - timerMillis >= settings.getEnableLearnHoldMillis()) {
      if (digitalRead(LEARN_LED_PIN) == LOW) {
        digitalWrite(LEARN_LED_PIN, HIGH);
      }
    }
  } else if (timerMillis > 0UL) {
    ulong nowMillis = millis();
    if (nowMillis - timerMillis > 30000UL) {
      triggerFactoryReset = true;
    } else if (nowMillis - timerMillis >= settings.getEnableLearnHoldMillis()) {
      triggerDeviceLearn = true;
    }
    timerMillis = 0UL;
    digitalWrite(LEARN_LED_PIN, LOW);
  } 
}

/**
 * Checks to see if a device is close enough to be a good
 * pair candidate and if so turns on the close device indicator
 * LED. If not it ensures the LED is off.
 */
void doCheckForCloseDevice() {
  bool isClose = false;
  for (const auto& pair : seenRssis) {
    if (pair.second >= settings.getCloseRssi()) {
      isClose = true;
      if (digitalRead(CLOSE_LED_PIN) == LOW) {
        digitalWrite(CLOSE_LED_PIN, HIGH);
      }

      break;
    }
  }

  if (!isClose && digitalRead(CLOSE_LED_PIN) == HIGH) {
    digitalWrite(CLOSE_LED_PIN, LOW);
  }
}

/**
 * Checks for a factory reset condition then performs the reset.
 * 
 */
void doCheckFactoryReset() {
  if (triggerFactoryReset) {
    Serial.println("Device Factory Reset!");
    ulong startMillis = millis();
    while (millis() - startMillis < 3500UL) {
      yield();
      digitalWrite(LEARN_LED_PIN, digitalRead(LEARN_LED_PIN) == HIGH ? LOW : HIGH);
      delay(100UL);
    }
    digitalWrite(LEARN_LED_PIN, LOW);
    
    settings.factoryDefault();
    ESP.restart();
  }
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
    if (
      settings.getParedAddress().equalsIgnoreCase("xx:xx:xx:xx:xx:xx") 
      || settings.getParedAddress().equalsIgnoreCase(id)
    ) { 
      Serial.printf("Purged 'seen' device; device=[%s]\n", id.c_str());
    }
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
    if (settings.getParedAddress().equalsIgnoreCase("xx:xx:xx:xx:xx:xx")) {
      Serial.printf("Near device; device=[%s]; rssid=[%d]\n", dev.address().c_str(), dev.rssi());
    } else if (settings.getParedAddress().equalsIgnoreCase(dev.address())) {
      Serial.printf("Device Checked In! DeviceID=[%s]; RSSI=[%d];\n", dev.address().c_str(), dev.rssi());
    }
    seenDevices[dev.address()] = millis();
    seenRssis[dev.address()] = dev.rssi();
  } else if (dev) {
    if (
      settings.getParedAddress().equalsIgnoreCase("xx:xx:xx:xx:xx:xx") 
      || settings.getParedAddress().equalsIgnoreCase(dev.address())
    ) {
      Serial.printf("Seen device RSSI too low! DeviceID=[%s]; RSSI=[%d];\n", dev.address().c_str(), dev.rssi());
    }
  }
}

/**
 * Handles the learning task. The learning task allows
 * for the device to identify and track the device which is nearest 
 * to it at the time the learning is performed.
 * 
 */
void doCheckLearnTask() {
  static ulong learnStartMillis = 0L;
  static bool learnStarted = false;

  if (triggerDeviceLearn) {
    // Do start of learning tasks
    if (!learnStarted) {
      digitalWrite(LEARN_LED_PIN, HIGH);
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
      learnStarted = false;
      triggerDeviceLearn = false;

      digitalWrite(LEARN_LED_PIN, LOW);
    }
  }
}