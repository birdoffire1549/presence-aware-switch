
/*
  Firmware: .... presence-aware-switch
  Version: ..... 2.0.2
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
#include <DNSServer.h>
#include <WebServer.h>

#include "HtmlContent.h"
#include <Utils.h>
#include <IpUtils.h>
#include <LedMan.h>

#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <BLEEddystoneURL.h>
#include <BLEEddystoneTLM.h>
#include <BLEBeacon.h>

#define PAIR_BTN_PIN 32
#define LEARN_LED_PIN 13
#define CONTROLLED_DEVICE_PIN 2
#define CLOSE_LED_PIN 17

#define INIT_ON_STATE false

#define FIRMWARE_VERSION "2.1.1"
#define DEBUG

Settings settings;
DNSServer dnsServer;
WebServer web(80);

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
void doHandleNetworkTasks();
void doActivateDeactivateWiFi();

void handleBTScanResults(BLEScanResults);
void handleSettingsPage();
void handleSettingsPost();

std::map<std::string, ulong> seenDevices;
std::map<std::string, int> seenRssis;

BLEScan *scan;
LedMan ledMan;

// Action Trigger Flags
bool triggerFactoryReset = false;
bool triggerDeviceLearn = false;
bool triggerWifiIsOn = false;

// State Flags
bool isLearning = false;
bool isScanning = false;
bool isWifiIsOn = false;

String deviceId = Utils::genDeviceIdFromMacAddr(WiFi.macAddress());
String deviceSsid = "ProxiSwitch_" + deviceId;
String settingsUpdateResult = "";

const String LEARN_LED_ID = "learn_led";
const String CLOSE_LED_ID = "close_led";

const String LEARN_FUNCTION_ID = "learn";
const String FACTORY_RESET_FUNCTION_ID = "factory";

const String WIFI_ENABLE_FUNCTION_ID = "wifi";
const String CLOSE_FUNCTION_ID = "close";

/**
 * SETUP
 * =======================================
 * The main setup portion of the firmware.
 * 
 */
void setup() {
  WiFi.mode(WIFI_OFF);
  pinMode(PAIR_BTN_PIN, INPUT);
  pinMode(CONTROLLED_DEVICE_PIN, OUTPUT);
  pinMode(LEARN_LED_PIN, OUTPUT);
  pinMode(CLOSE_LED_PIN, OUTPUT);

  settings.loadSettings();

  digitalWrite(CONTROLLED_DEVICE_PIN, settings.isOnState() ? HIGH : LOW);
  digitalWrite(LEARN_LED_PIN, LOW);
  digitalWrite(CLOSE_LED_PIN, LOW);

  ledMan.addLed(LEARN_LED_PIN, LEARN_LED_ID);
  ledMan.addLed(CLOSE_LED_PIN, CLOSE_LED_ID);

  ledMan.setCallerPriority(FACTORY_RESET_FUNCTION_ID, 1);
  ledMan.setCallerPriority(LEARN_FUNCTION_ID, 2);
  ledMan.setCallerPriority(WIFI_ENABLE_FUNCTION_ID, 5);
  ledMan.setCallerPriority(CLOSE_FUNCTION_ID, 10);

  #ifdef DEBUG
    // Initialize Serial for Output
    Serial.begin(115200);
    if (!Serial) ESP.restart();
  #endif

  BLEDevice::init("");

  #ifdef DEBUG
    Serial.print(F("Reinitializing Bluetooth... "));
  #endif
  scan = BLEDevice::getScan();
  scan->setActiveScan(true);  //active scan uses more power, but get results faster
  scan->setInterval(100);
  scan->setWindow(99);  // less or equal setInterval value
  scan->start(5, handleBTScanResults);
  isScanning = true;
  #ifdef DEBUG
    Serial.println(F("Complete."));

    Serial.printf("Learn Hold: %d millis\n", settings.getEnableLearnHoldMillis());
    Serial.printf("Learn Wait: %d millis\n", settings.getLearnWaitMillis());
    Serial.printf("Max Not Seen: %d millis\n", settings.getMaxNotSeenMillis());
    Serial.printf("Max Near RSSI: %d \n", settings.getMaxNearRssi());
    Serial.printf("Paired Address: %s\n", settings.getParedAddress().c_str());
  #endif
}

/**
 * MAIN LOOP
 * ======================================
 * The main looping part of the firmware.
 * 
 */
void loop() {
  ledMan.loop();
  doBTScan();
  doHandleOnOffSwitching();
  doCheckForCloseDevice();
  doHandleButtonPresses();
  doCheckFactoryReset();
  doCheckLearnTask();
  doHandleNetworkTasks();

  yield();
}

/**
 * This function handles all tasks related to network.
 * This includes turning on and off networking, web server and
 * DNS as well as the looping functons needed to answer requests.
 * 
 */
void doHandleNetworkTasks() {
  doActivateDeactivateWiFi();
  if (isWifiIsOn) {
    dnsServer.processNextRequest();
    web.handleClient();
  }
}

void doActivateDeactivateWiFi() {
  if (triggerWifiIsOn && !isWifiIsOn) {
    ledMan.lockLed(CLOSE_LED_ID, WIFI_ENABLE_FUNCTION_ID);

    // Need to turn on all networking and services
    #ifdef DEBUG
      Serial.print(F("Starting WiFi AP Mode... "));
    #endif

    WiFi.setHostname((String(F("PxiSw_")) + deviceId).c_str());
    WiFi.setMinSecurity(WIFI_AUTH_WPA2_PSK);
    WiFi.softAPConfig(
      IpUtils::stringIPv4ToIPAddress(F("192.168.4.1")), 
      IpUtils::stringIPv4ToIPAddress(F("192.168.4.1")), 
      IpUtils::stringIPv4ToIPAddress(F("255.255.255.0"))
    );

    WiFi.softAP(deviceSsid, settings.getApPwd());
    WiFi.enableAP(true);
    
    #ifdef DEBUG
      Serial.println(F("Complete."));
      Serial.println(F("Starting DNS for captive portal... "));
    #endif

    dnsServer.start(53u, "*", IpUtils::stringIPv4ToIPAddress(F("192.168.4.1")));
      
    #ifdef DEBUG
      Serial.println(F("Complete."));
      Serial.print(F("Initializing Web Services... "));
    #endif

    web.on("/", handleSettingsPage);
    web.onNotFound(handleSettingsPage);
    web.begin();

    #ifdef DEBUG
      Serial.println(F("Complete."));
    #endif

    isWifiIsOn = true;
  } else if (triggerWifiIsOn) {
    static ulong timmerMillis = 0UL;
    if (millis() - timmerMillis > 50UL) {
      ledMan.ledToggle(CLOSE_LED_ID, WIFI_ENABLE_FUNCTION_ID);
      timmerMillis = millis();
    }
  } else if (!triggerWifiIsOn && isWifiIsOn) {
    ledMan.releaseLed(CLOSE_LED_ID, WIFI_ENABLE_FUNCTION_ID);
    ledMan.ledOff(CLOSE_LED_ID, WIFI_ENABLE_FUNCTION_ID);

    // Need to turn off WiFi and Services
    #ifdef DEBUG
      Serial.print(F("Stopping DNS Server... "));
    #endif

    dnsServer.stop();
    
    #ifdef DEBUG
      Serial.println(F("Complete."));
      Serial.print(F("Stopping web Server... "));
    #endif

    web.stop();
    
    #ifdef DEBUG
      Serial.println(F("Complete."));
      Serial.print(F("Stopping WiFi AP... "));
    #endif

    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    
    #ifdef DEBUG
      Serial.println(F("Complete."));
    #endif
    
    isWifiIsOn = false;
  }
}

/**
 * This function is the sole handler of the learn button's 
 * functionality. It notifies other functions when various tasks
 * need to be performed using boolean event flags.
 * 
 * NOTE: Wifi must be off for factory reset or learning to be able
 * to be triggered. Once factory reset or learning is in progress the
 * button's functionality is disabled.
 * 
 */
void doHandleButtonPresses() {
  const ulong minWifiEnableMillis = 1500UL;
  const ulong minFactoryHoldMillis = 30000UL;

  if (!triggerDeviceLearn && !triggerFactoryReset) {
    static ulong timerMillis = 0UL;
    ulong elapsedMillis = millis() - timerMillis;

    if (digitalRead(PAIR_BTN_PIN) == HIGH) {
      if (timerMillis == 0UL) {
        timerMillis = millis();
        elapsedMillis = 0UL;
      }
      if (!triggerWifiIsOn && elapsedMillis > minFactoryHoldMillis) {
        ledMan.ledOff(LEARN_LED_ID, LEARN_FUNCTION_ID);
        ledMan.lockLed(LEARN_LED_ID, FACTORY_RESET_FUNCTION_ID);
        // Flashing learning LED to signal factory reset on release
        for (int i = 0; i < 4; i++) {
          ledMan.ledToggle(LEARN_LED_ID, FACTORY_RESET_FUNCTION_ID);
          ledMan.loop();
          delay(50UL);
        }
      } else if (
        !triggerWifiIsOn 
        && elapsedMillis >= settings.getEnableLearnHoldMillis()
      ) {
        // Turn on learning LED Solid to signal function triggered if released
        ledMan.releaseLed(CLOSE_LED_ID, WIFI_ENABLE_FUNCTION_ID);
        ledMan.ledOff(CLOSE_LED_ID, WIFI_ENABLE_FUNCTION_ID);
        ledMan.ledOn(LEARN_LED_ID, LEARN_FUNCTION_ID);
      } else if (
        elapsedMillis > minWifiEnableMillis 
        && (
          elapsedMillis < settings.getEnableLearnHoldMillis()
          || triggerWifiIsOn
        )
      ) {
        // Flashing Close LED to signal WiFi on/off if released
        ledMan.lockLed(CLOSE_LED_ID, WIFI_ENABLE_FUNCTION_ID);
        if (!triggerWifiIsOn) {
          static ulong subTimerMillis = 0UL;
          if (millis() - subTimerMillis > 50UL) {
            ledMan.ledToggle(CLOSE_LED_ID, WIFI_ENABLE_FUNCTION_ID);
            subTimerMillis = millis();
          }
        } else {
          ledMan.ledOff(CLOSE_LED_ID, WIFI_ENABLE_FUNCTION_ID);
        }
      } 
    } else if (timerMillis > 0UL) {
      // There was a button press; Evaluate the length for functionality
      if (elapsedMillis > minFactoryHoldMillis) {
        // Super Long Hold - Factory Reset
        triggerFactoryReset = true;
      } else if (elapsedMillis >= settings.getEnableLearnHoldMillis()) {
        // Long Press - Learning Mode
        triggerDeviceLearn = true;
      } else if (
        elapsedMillis > minWifiEnableMillis 
        && elapsedMillis < settings.getEnableLearnHoldMillis()
      ) {
        // Short Press - WiFi On/Off
        triggerWifiIsOn = !triggerWifiIsOn;
      }

      timerMillis = 0UL;
      ledMan.ledOff(LEARN_LED_ID, LEARN_FUNCTION_ID);
      ledMan.releaseLed(CLOSE_LED_ID, WIFI_ENABLE_FUNCTION_ID);
      ledMan.ledOff(CLOSE_LED_ID, WIFI_ENABLE_FUNCTION_ID);
      ledMan.releaseLed(LEARN_LED_ID, FACTORY_RESET_FUNCTION_ID);
      ledMan.ledOff(LEARN_LED_ID, FACTORY_RESET_FUNCTION_ID);
    } 
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
      ledMan.ledOn(CLOSE_LED_ID, CLOSE_FUNCTION_ID);

      break;
    }
  }

  if (!isClose) {
    ledMan.ledOff(CLOSE_LED_ID, CLOSE_FUNCTION_ID);
  }
}

/**
 * Checks for a factory reset condition then performs the reset.
 * 
 */
void doCheckFactoryReset() {
  if (triggerFactoryReset) {
    #ifdef DEBUG
      Serial.println(F("Device Factory Reset!"));
    #endif
    ulong startMillis = millis();
    ledMan.lockLed(LEARN_LED_ID, FACTORY_RESET_FUNCTION_ID);
    while (millis() - startMillis < 3500UL) {
      yield();
      ledMan.ledToggle(LEARN_LED_ID, FACTORY_RESET_FUNCTION_ID);
      ledMan.loop();
      delay(100UL);
    }
    ledMan.releaseLed(LEARN_LED_ID, FACTORY_RESET_FUNCTION_ID);
    ledMan.ledOff(LEARN_LED_ID, FACTORY_RESET_FUNCTION_ID);
    
    settings.factoryDefault();
    #ifdef DEBUG
      Serial.println(F("Factory reset complete; Rebooting ESP now!"));
    #endif
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
    #ifdef DEBUG
      Serial.println(F("Device: ON!!!"));
    #endif
  } else if (!settings.isOnState() && digitalRead(CONTROLLED_DEVICE_PIN) == HIGH) {
    // Device is on but should be off; Turn it off
    digitalWrite(CONTROLLED_DEVICE_PIN, LOW);
    #ifdef DEBUG
      Serial.println(F("Device: OFF!!!"));
    #endif
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
      #ifdef DEBUG
        if (
          settings.getParedAddress().equalsIgnoreCase(F("xx:xx:xx:xx:xx:xx")) 
          || settings.getParedAddress().equalsIgnoreCase(String(id.c_str()))
        ) { 
          Serial.printf("Purged 'seen' device; device=[%s]\n", id.c_str());
        }
      #endif
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
      ledMan.ledOn(LEARN_LED_ID, LEARN_FUNCTION_ID);
      learnStartMillis = millis();
      #ifdef DEBUG
        Serial.println(F("Learning started..."));
      #endif
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
        #ifdef DEBUG
          Serial.printf("Learning Complete! Paired Device is '%s', with RSSI of: %d\n\n", nearestId.c_str(), nearestRssi);
        #endif
      } else {
        Serial.println(F("Learning Complete! Paired Device is same as previous!\n"));
      }

      // Do end of learning tasks
      isLearning = false;
      triggerDeviceLearn = false;

      ledMan.ledOff(LEARN_LED_ID, LEARN_FUNCTION_ID);
    }
  }
}

/**
 * Handles showing the settings page and filling out all
 * the dynamic content on the page.
 * 
 */
void handleSettingsPage() {
  if (web.method() == HTTP_POST) {
    handleSettingsPost();
  }

  String page = String(SETTINGS_PAGE);

  page.replace(F("${message}"), settingsUpdateResult);
  settingsUpdateResult = "";

  page.replace(F("${version}"), FIRMWARE_VERSION);
  page.replace(F("${ap_pwd}"), settings.getApPwd());
  page.replace(F("${close_rssi}"), String(settings.getCloseRssi()));
  page.replace(F("${max_rssi}"), String(settings.getMaxNearRssi()));
  page.replace(F("${max_seen}"), String(settings.getMaxNotSeenMillis()));
  page.replace(F("${learn_trigger}"), String(settings.getEnableLearnHoldMillis()));
  page.replace(F("${learn_wait}"), String(settings.getLearnWaitMillis()));
  page.replace(F("${pared_address}"), settings.getParedAddress());

  web.send(200, F("text/html"), page.c_str());
  yield();
}

/**
 * Handles the setting page when a POST method is made with 
 * updates to the settings.
 * This function stores the new settings and reboots the device
 * if needed.
 * 
 */
void handleSettingsPost() {
  String newApPwd = web.arg(F("ap_pwd"));
  String newMaxRssi = web.arg(F("max_rssi"));
  String newCloseRssi = web.arg(F("close_rssi"));
  String newMaxSeenMillis = web.arg(F("max_seen"));
  String newLearnWaitMillis = web.arg(F("learn_wait"));
  String newLearnTriggerMillis = web.arg(F("learn_trigger"));
  
  if (
    newApPwd && !newApPwd.isEmpty()
    && newMaxRssi && !newMaxRssi.isEmpty()
    && newCloseRssi && !newCloseRssi.isEmpty()
    && newMaxSeenMillis && !newMaxSeenMillis.isEmpty()
    && newLearnWaitMillis && !newLearnWaitMillis.isEmpty()
    && newLearnTriggerMillis && !newLearnTriggerMillis.isEmpty()
  ) {
    bool needSave = false;
    bool needReboot = false;

    if (!settings.getApPwd().equals(newApPwd)) {
      needSave = true;
      needReboot = true;
      settings.setApPwd(newApPwd);
    }

    int intVal = newMaxRssi.toInt();
    if (settings.getMaxNearRssi() != intVal) {
      needSave = true;
      settings.setMaxNearRssi(intVal);
    }

    intVal = newCloseRssi.toInt();
    if (settings.getCloseRssi() != intVal) {
      needSave = true;
      settings.setCloseRssi(intVal);
    }

    unsigned long ulVal = newMaxSeenMillis.toDouble();
    if (settings.getMaxNotSeenMillis() != ulVal) {
      needSave = true;
      settings.setMaxNotSeenMillis(ulVal);
    }

    ulVal = newLearnTriggerMillis.toDouble();
    if (settings.getEnableLearnHoldMillis() != ulVal) {
      needSave = true;
      settings.setEnableLearnHoldMillis(ulVal);
    }

    ulVal = newLearnWaitMillis.toDouble();
    if (settings.getLearnWaitMillis() != ulVal) {
      needSave = true;
      settings.setLearnWaitMillis(ulVal);
    }

    if (needSave) {
      bool ok = settings.saveSettings();
        if (ok) {
          settingsUpdateResult = String(SUCCESSFUL);
          #ifdef DEBUG
            Serial.println(F("Settings Updated!"));
          #endif
        } else {
          settingsUpdateResult = String(FAILED);
          #ifdef DEBUG
            Serial.println(F("Settings update Failed!!!"));
          #endif
        }
      
      if (needReboot) {
        #ifdef DEBUG
          Serial.println(F("Device rebooting to settings..."));
          delay(10000UL);
        #endif
        ESP.restart();
      }
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
      if (isLearning || settings.getParedAddress().equalsIgnoreCase(F("xx:xx:xx:xx:xx:xx"))) {
        // Record all seen in-range if learning or not paired
        #ifdef DEBUG
          Serial.printf("Near device; device=[%s]; rssid=[%d]\n", btAddress.c_str(), rssi);
        #endif
        seenDevices[btAddress.c_str()] = millis();
        seenRssis[btAddress.c_str()] = rssi;
      } else if (settings.getParedAddress().equalsIgnoreCase(btAddress)) {
        // Only record device being tracked
        #ifdef DEBUG
          Serial.printf("Device Checked In! DeviceID=[%s]; RSSI=[%d];\n", btAddress.c_str(), rssi);
        #endif
        seenDevices[btAddress.c_str()] = millis();
        seenRssis[btAddress.c_str()] = rssi;
      }
    } else {
      // Seen device is out of range just log it
      #ifdef DEBUG
        if (
          settings.getParedAddress().equalsIgnoreCase(F("xx:xx:xx:xx:xx:xx")) 
          || settings.getParedAddress().equalsIgnoreCase(btAddress)
        ) {
          Serial.printf("Seen device RSSI too low! DeviceID=[%s]; RSSI=[%d];\n", btAddress.c_str(), rssi);
        }
      #endif
    }
  }
  isScanning = false;
}