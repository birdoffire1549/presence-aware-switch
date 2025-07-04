
/*
  Firmware: .... presence-aware-switch
  Version: ..... 1.3.0
  Hardware: .... ESP-32
  Author: ...... Scott Griffis
  Date: ........ 07/04/2025
  
  Description:
    This firmware is intended for an Expressif ESP-32 device. It allows for the 
    device to control an outlet based on presence. It does this by allowing a
    Bluetooth LE beacon to be paired with the device. When the beacon is within 
    range then the associated outlet is powered on. When the beacon is missing
    then the accociated outlet is powered off.
*/

#include <Arduino.h>
#include <Settings.h>
#include <map>
#include <vector>
#include <WiFi.h>

#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <BLEEddystoneURL.h>
#include <BLEEddystoneTLM.h>
#include <BLEBeacon.h>

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

void handleBTScanResults(BLEScanResults);

std::map<std::string, ulong> seenDevices;
std::map<std::string, int> seenRssis;

BLEScan *scan;

// Action Trigger Flags
bool triggerFactoryReset = false;
bool triggerDeviceLearn = false;

// State Flags
bool isLearning = false;
bool isScanning = false;

/**
 * SETUP
 * =======================================
 * The main setup portion of the firmware.
 * 
 */
void setup() {
  WiFi.mode(WIFI_OFF);
  pinMode(PAIR_PIN, INPUT);
  pinMode(CONTROLLED_DEVICE_PIN, OUTPUT);
  pinMode(LEARN_LED_PIN, OUTPUT);
  pinMode(CLOSE_LED_PIN, OUTPUT);

  settings.loadSettings();

  digitalWrite(CONTROLLED_DEVICE_PIN, settings.isOnState() ? HIGH : LOW);
  digitalWrite(LEARN_LED_PIN, LOW);
  digitalWrite(CLOSE_LED_PIN, LOW);

  // Initialize Serial for Output
  Serial.begin(115200);
  if (!Serial) ESP.restart();

  BLEDevice::init("");

  Serial.print("Reinitializing Bluetooth... ");
  scan = BLEDevice::getScan();
  scan->setActiveScan(true);  //active scan uses more power, but get results faster
  scan->setInterval(100);
  scan->setWindow(99);  // less or equal setInterval value
  scan->start(5, handleBTScanResults);
  isScanning = true;
  Serial.println("Complete.");

  Serial.printf("Learn Hold: %d millis\n", settings.getEnableLearnHoldMillis());
  Serial.printf("Learn Wait: %d millis\n", settings.getLearnWaitMillis());
  Serial.printf("Max Not Seen: %d millis\n", settings.getMaxNotSeenMillis());
  Serial.printf("Max Near RSSI: %d \n", settings.getMaxNearRssi());
  Serial.printf("Paired Address: %s\n", settings.getParedAddress().c_str());
}

/**
 * MAIN LOOP
 * ======================================
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
 * 
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
 * 
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
  settings.setOnState(seenDevices.count(settings.getParedAddress().c_str()) > 0);
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
 * Used to purge expired seen devices which are no longer considered
 * to be in-range.
 * 
 */
void doPurgeOldSeenDevices() {
  // Purge seenDevices that are expired
  int devCount = seenDevices.size();
  if (devCount > 0) {
    std::vector<std::string> purgeList;

    // Locate expired devices which need purged
    for (const auto& pair : seenDevices) {
      if (millis() - pair.second > settings.getMaxNotSeenMillis()) {
        purgeList.push_back(pair.first);
      }
    }

    // Purge the identified expired devices
    for (std::string id : purgeList) {
      seenDevices.erase(id);
      seenRssis.erase(id);
      if (
        settings.getParedAddress().equalsIgnoreCase("xx:xx:xx:xx:xx:xx") 
        || settings.getParedAddress().equalsIgnoreCase(String(id.c_str()))
      ) { 
        Serial.printf("Purged 'seen' device; device=[%s]\n", id.c_str());
      }
    }
    purgeList.clear();
  }
}

/**
 * Handles tasks related to BlueTooth scans by first
 * purging stored devices not seen past their expiration
 * time, then it kicks off the scan again if it has completed.
 * 
 */
void doBTScan() {
  doPurgeOldSeenDevices();

  if (!isScanning) {
    // Start scanning when it is done
    scan->start(5, handleBTScanResults);
    isScanning = true;
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
  
  if (triggerDeviceLearn) {
    // Do start of learning tasks
    if (!isLearning) {
      digitalWrite(LEARN_LED_PIN, HIGH);
      learnStartMillis = millis();
      Serial.printf("Learning started...\n");
      isLearning = true;
    }

    // Wait 10 Seconds to allow nearest discovery then pair with nearest
    if (millis() - learnStartMillis > settings.getLearnWaitMillis()) {
      std::string nearestId = "";
      int nearestRssi = -999;

      // Check known devices for nearest
      for (const auto& pair : seenDevices) {
        if (String(nearestId.c_str()).isEmpty() || seenRssis[pair.first] > nearestRssi) {
          nearestId = pair.first;
          nearestRssi = seenRssis[pair.first];
        }
      }

      // Pair with identified ID
      if (!settings.getParedAddress().equalsIgnoreCase(String(nearestId.c_str()))) {
        settings.setParedAddress(String(nearestId.c_str()));
        settings.saveSettings();
        Serial.printf("Learning Complete! Paired Device is '%s', with RSSI of: %d\n\n", nearestId.c_str(), nearestRssi);
      } else {
        Serial.printf("Learning Complete! Paired Device is same as previous!\n\n");
      }

      // Do end of learning tasks
      isLearning = false;
      triggerDeviceLearn = false;

      digitalWrite(LEARN_LED_PIN, LOW);
    }
  }
}

/**
 * This function is used to handle BlueTooth LE scan results.
 * Essentially it logs the information about the devices that 
 * were seen during the scan.
 * 
 * When not tracking a specific device or in learning mode, 
 * any device with a rssi lower than the acceptable max is 
 * ignored while those with acceptable rssi's are recorded.
 * 
 * When tracking a specific device all devices except that 
 * device are ignored. 
 */
void handleBTScanResults(BLEScanResults results) {
  for (int i = 0; i < results.getCount(); i++) {
    // Iterate and handle scanned devices
    BLEAdvertisedDevice device = results.getDevice(i);

    String btAddress = String(device.getAddress().toString().c_str());
    int rssi = device.getRSSI();
    
    if (rssi > settings.getMaxNearRssi()) {
      // Saw a device that is in-range
      if (isLearning || settings.getParedAddress().equalsIgnoreCase("xx:xx:xx:xx:xx:xx")) {
        // Record all seen in-range if learning or not paired
        Serial.printf("Near device; device=[%s]; rssid=[%d]\n", btAddress.c_str(), rssi);
        seenDevices[btAddress.c_str()] = millis();
        seenRssis[btAddress.c_str()] = rssi;
      } else if (settings.getParedAddress().equalsIgnoreCase(btAddress)) {
        // Only record device being tracked
        Serial.printf("Device Checked In! DeviceID=[%s]; RSSI=[%d];\n", btAddress.c_str(), rssi);
        seenDevices[btAddress.c_str()] = millis();
        seenRssis[btAddress.c_str()] = rssi;
      }
    } else {
      // Seen device is out of range just log it
      if (
        settings.getParedAddress().equalsIgnoreCase("xx:xx:xx:xx:xx:xx") 
        || settings.getParedAddress().equalsIgnoreCase(btAddress)
      ) {
        Serial.printf("Seen device RSSI too low! DeviceID=[%s]; RSSI=[%d];\n", btAddress.c_str(), rssi);
      }
    }
  }
  isScanning = false;
}