
/*
  Firmware: .... presence-aware-switch
  Version: ..... 2.0.0
  Hardware: .... ESP-32
  Author: ...... Scott Griffis
  Date: ........ 06/15/2025
  
  Description:
    This firmware is intended for an Expressif ESP-32 device. It allows for the 
    device to control an outlet based on presence. It does this by allowing a
    Bluetooth LE beacon to be paired with the device. When the beacon is within 
    range then the associated outlet is powered on. When the beacon is missing
    then the accociated outlet is powered off.

    Settings can be modified for the device using an internally hosted configuration
    page via web browser.
*/

#include <Arduino.h>
#include <ArduinoBLE.h>

#include <map>
#include <vector>
#include <HtmlContent.h>

// Includes for custom utilities and functionality
#include <Settings.h>
#include <Utils.h>
#include <IpUtils.h>

// Includes to support Web Service
#include <WiFi.h>
#include <DNSServer.h>
#include <WebServer.h>

// ************************************************************************************
// Define Statements
// ************************************************************************************
#define FIRMWARE_VERSION "2.0.1"
#define DEBUG // Enables Debug logging via serial, comment out for prod build.

#define PAIR_PIN 32
#define LEARN_LED_PIN 13
#define CONTROLLED_DEVICE_PIN 2
#define CLOSE_LED_PIN 17

#define INIT_ON_STATE false

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
void doHandleNetworkTasks();
void doCheckFactoryReset();
void doActivateDeactivateWiFi();

void handleSettingsPage();
void handleSettingsPost();

std::map<String, ulong> seenDevices;
std::map<String, int> seenRssis;
std::vector<String> purgeList;

bool triggerFactoryReset = false;
bool triggerDeviceLearn = false;
bool triggerWifiIsOn = false;

bool wifiIsOn = false;

String deviceId = Utils::genDeviceIdFromMacAddr(WiFi.macAddress());
String deviceSsid = "ProxiSwitch_" + deviceId;
String settingsUpdateResult = "";


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
  #ifdef DEBUG
    Serial.begin(9600);
    if (!Serial) ESP.restart();
  #endif

  // Initialize Bluetooth
  #ifdef DEBUG
    Serial.print(F("Starting Bluetooth... "));
  #endif

  if (!BLE.begin()) {
    #ifdef DEBUG
      Serial.println(F("Issue with starting Bluetooth!"));
      delay(5000L);
    #endif
    ESP.restart();
  }
  #ifdef DEBUG
    Serial.println(F("Complete."));

    Serial.printf("Learn Hold: %d millis\n", settings.getEnableLearnHoldMillis());
    Serial.printf("Learn Wait: %d millis\n", settings.getLearnWaitMillis());
    Serial.printf("Max Not Seen: %d millis\n", settings.getMaxNotSeenMillis());
    Serial.printf("Max Near RSSI: %d \n", settings.getMaxNearRssi());
    Serial.printf("Paired Address: %s\n", settings.getParedAddress().c_str());
  #endif

  WiFi.disconnect(true);
  WiFi.softAPdisconnect(true);

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
  doHandleNetworkTasks();
  yield();
}

void doHandleNetworkTasks() {
  doActivateDeactivateWiFi();
  if (wifiIsOn) {
    dnsServer.processNextRequest();
    web.handleClient();
  }
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
      // Press longer than 30 seconds does factory reset
      triggerFactoryReset = true;
    } else if (nowMillis - timerMillis >= settings.getEnableLearnHoldMillis()) {
      // Press longer than set amount triggers Learn
      triggerDeviceLearn = true;
    } else if (nowMillis - timerMillis > 500UL && nowMillis - timerMillis < settings.getEnableLearnHoldMillis()) {
      // Press less than Learn setting turns on/off wifi
      triggerWifiIsOn = !triggerWifiIsOn;
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
    #ifdef DEBUG
      Serial.println(F("Device Factory Reset!"));
    #endif
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
  settings.setOnState(seenDevices.count(settings.getParedAddress()) > 0);
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
    #ifdef DEBUG
      if (
        settings.getParedAddress().equalsIgnoreCase("xx:xx:xx:xx:xx:xx") 
        || settings.getParedAddress().equalsIgnoreCase(id)
      ) { 
        Serial.printf("Purged 'seen' device; device=[%s]\n", id.c_str());
      }
    #endif
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
  BLE.poll();
  BLEDevice dev = BLE.available();
  if (dev && dev.rssi() > settings.getMaxNearRssi()) {
    Serial.printf("DEBUG#1: rssi=[%d]; maxNear=[%d];\n", dev.rssi(), settings.getMaxNearRssi());
    // Seen device is not out of range
    #ifdef DEBUG
      if (settings.getParedAddress().equalsIgnoreCase("xx:xx:xx:xx:xx:xx")) {
        Serial.printf("Near device; device=[%s]; rssid=[%d]\n", dev.address().c_str(), dev.rssi());
      } else if (settings.getParedAddress().equalsIgnoreCase(dev.address())) {
        Serial.printf("Device Checked In! DeviceID=[%s]; RSSI=[%d];\n", dev.address().c_str(), dev.rssi());
      }
    #endif
    seenDevices[dev.address()] = millis();
    seenRssis[dev.address()] = dev.rssi();
  } else if (dev) {
    #ifdef DEBUG
      if (
        settings.getParedAddress().equalsIgnoreCase("xx:xx:xx:xx:xx:xx") 
        || settings.getParedAddress().equalsIgnoreCase(dev.address())
      ) {
        Serial.printf("Seen device RSSI too low! DeviceID=[%s]; RSSI=[%d];\n", dev.address().c_str(), dev.rssi());
      }
    #endif
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
      #ifdef DEBUG
        Serial.println(F("Learning started..."));
      #endif
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
        #ifdef DEBUG
          Serial.printf("Learning Complete! Paired Device is '%s', with RSSI of: %d\n\n", nearestId.c_str(), nearestRssi);
        #endif
      } else {
        #ifdef DEBUG
          Serial.println(F("Learning Complete! Paired Device is same as previous!\n"));
        #endif
      }

      // Do end of learning tasks
      learnStarted = false;
      triggerDeviceLearn = false;

      digitalWrite(LEARN_LED_PIN, LOW);
    }
  }
}

void doActivateDeactivateWiFi() {
  if (triggerWifiIsOn && !wifiIsOn) {
    #ifdef DEBUG
      Serial.print(F("Starting WiFi AP Mode..."));
    #endif

    WiFi.setHostname((String("PxiSw_") + deviceId).c_str());
    WiFi.setMinSecurity(WIFI_AUTH_WPA2_PSK);
    WiFi.softAPConfig(
      IpUtils::stringIPv4ToIPAddress(F("192.168.4.1")), 
      IpUtils::stringIPv4ToIPAddress(F("192.168.4.1")), 
      IpUtils::stringIPv4ToIPAddress(F("255.255.255.0"))
    );

    WiFi.softAP(deviceSsid, settings.getApPwd());
    WiFi.enableAP(true);
    
    #ifdef DEBUG
      Serial.println(F(" Complete."));
      Serial.println(F("Starting DNS for captive portal..."));
    #endif

    dnsServer.start(53u, "*", IpUtils::stringIPv4ToIPAddress(F("192.168.4.1")));
      
    #ifdef DEBUG
      Serial.println(F(" Complete."));
      Serial.print(F("Initializing Web Services..."));
    #endif

    web.on("/", handleSettingsPage);
    web.onNotFound(handleSettingsPage);
    web.begin();

    #ifdef DEBUG
      Serial.println(F(" Complete."));
    #endif

    wifiIsOn = true;
  } else if (triggerWifiIsOn) {
    static ulong timmerMillis = 0UL;
    if (millis() - timmerMillis > 10UL) {
      digitalWrite(LEARN_LED_PIN, (LEARN_LED_PIN == HIGH) ? LOW : HIGH);
    }
  } else if (!triggerWifiIsOn && wifiIsOn) {
    #ifdef DEBUG
      Serial.print(F("Stopping DNS Server..."));
    #endif
    dnsServer.stop();
    #ifdef DEBUG
      Serial.println(F(" Complete."));
      Serial.print(F("Stopping web Server..."));
    #endif
    web.stop();
    #ifdef DEBUG
      Serial.println(F(" Complete."));
      Serial.print(F("Stopping WiFi AP..."));
    #endif
    WiFi.softAPdisconnect(true);
    #ifdef DEBUG
      Serial.println(F(" Complete."));
    #endif
    
    wifiIsOn = false;
    if (digitalRead(LEARN_LED_PIN) == HIGH) {
      digitalWrite(LEARN_LED_PIN, LOW);
    }
  }
}

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

  web.send(200, "text/html", page.c_str());
  yield();
}

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
