
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
#include <HTTPSServer.hpp> // Have to replace '#include <hwcrypto/sha.h>' with '#include <esp32/sha.h>' in HTTPConnection.hpp
#include <SSLCert.hpp>
#include <HTTPRequest.hpp>
#include <HTTPResponse.hpp>

// ************************************************************************************
// Define Statements
// ************************************************************************************
#define FIRMWARE_VERSION "2.0.0"
#define DEBUG 1

#define PAIR_PIN 32
#define LEARN_LED_PIN 13
#define CONTROLLED_DEVICE_PIN 2
#define CLOSE_LED_PIN 17

#define INIT_ON_STATE false

using namespace httpsserver;

Settings settings;
DNSServer dnsServer;

// Create an SSL certificate object from the files included above
SSLCert * cert = new SSLCert();

// The function takes the following paramters:
// - Key size: 1024 or 2048 bit should be fine here, 4096 on the ESP might be "paranoid mode"
//   (in generel: shorter key = faster but less secure)
// - Distinguished name: The name of the host as used in certificates.
//   If you want to run your own DNS, the part after CN (Common Name) should match the DNS
//   entry pointing to your ESP32. You can try to insert an IP there, but that's not really good style.
// - Dates for certificate validity (optional, default is 2019-2029, both included)
//   Format is YYYYMMDDhhmmss
int createCertResult = createSelfSignedCert(
  *cert,
  KEYSIZE_2048,
  "CN=*,O=APerson,C=US",
  "20250101000000",
  "21250101000000"
);

// First, we create the HTTPSServer with the certificate created above
HTTPSServer secureServer = HTTPSServer(cert);

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

// Web Handler Prototypes
// ----------------------------------------------------
void handleRootGet(HTTPRequest * req, HTTPResponse * res);
void handleRootPost(HTTPRequest * req, HTTPResponse * res);
void handle404(HTTPRequest * req, HTTPResponse * res);

std::map<String, ulong> seenDevices;
std::map<String, int> seenRssis;
std::vector<String> purgeList;

bool triggerFactoryReset = false;
bool triggerDeviceLearn = false;

String deviceId = Utils::genDeviceIdFromMacAddr(WiFi.macAddress());
String deviceSsid = "ProxSwitch_" + deviceId;

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
    Serial.print("Starting Bluetooth... ");
  #endif

  if (!BLE.begin()) {
    #ifdef DEBUG
      Serial.println("Issue with starting Bluetooth!");
    #endif
    delay(5000L);
    ESP.restart();
  }
  #ifdef DEBUG
    Serial.println("Complete.");

    Serial.printf("Learn Hold: %d millis\n", settings.getEnableLearnHoldMillis());
    Serial.printf("Learn Wait: %d millis\n", settings.getLearnWaitMillis());
    Serial.printf("Max Not Seen: %d millis\n", settings.getMaxNotSeenMillis());
    Serial.printf("Max Near RSSI: %d \n", settings.getMaxNearRssi());
    Serial.printf("Paired Address: %s\n", settings.getParedAddress().c_str());
  #endif

  BLE.scan();

  if (createCertResult != 0) {
    #ifdef DEBUG
      Serial.printf("Cerating certificate failed. Error Code = 0x%02X, check SSLCert.hpp for details", createCertResult);
    #endif
  } else {
    #ifdef DEBUG
      Serial.println("Creating the SSL certificate was successful!");
      Serial.println("Starting WiFi AP Mode...");
    #endif

    WiFi.setTxPower(WIFI_POWER_19_5dBm);
    WiFi.setHostname((String("ProxSwitch_") + deviceId).c_str());
    WiFi.setMinSecurity(WIFI_AUTH_WPA2_WPA3_PSK);
    WiFi.softAPConfig(
      IpUtils::stringIPv4ToIPAddress("192.168.4.1"), 
      IpUtils::stringIPv4ToIPAddress("0.0.0.0"), 
      IpUtils::stringIPv4ToIPAddress("255.255.255.0")
    );
    WiFi.softAP(deviceSsid.c_str(), settings.getApPwd().c_str());
    dnsServer.start(53u, "*", IpUtils::stringIPv4ToIPAddress("192.168.4.1"));

    ulong startMillis = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startMillis < 5000UL) {
      yield();
    }

    #ifdef DEBUG
      if (WiFi.status() == WL_CONNECTED) {
        Serial.println("AP setup complete.");
      } else {
        Serial.println("AP Failed to Initialize! Skipping it.");
      }
    #endif
    
    #ifdef DEBUG
      Serial.println("Initializing Secure Web Services...");
    #endif
    
    ResourceNode * nodeRootGet = new ResourceNode("/", "GET", &handleRootGet);
    ResourceNode * nodeRootPost = new ResourceNode("/", "POST", &handleRootPost);
    ResourceNode * node404 = new ResourceNode("", "GET", &handle404);
    
    secureServer.registerNode(nodeRootGet);
    secureServer.registerNode(nodeRootPost);
    secureServer.setDefaultNode(node404);

    #ifdef DEBUG
      Serial.println("Starting server...");
    #endif
    secureServer.start();
    #ifdef DEBUG
      if (secureServer.isRunning()) {
        Serial.println("Server ready.");
      } else {
        Serial.println("Server Failed to Start!");
      }
    #endif
  }
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
  dnsServer.processNextRequest();
  secureServer.loop();
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
    #ifdef DEBUG
      Serial.println("Device Factory Reset!");
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
    //Serial.printf("Device: ON!!!\n");
  } else if (!settings.isOnState() && digitalRead(CONTROLLED_DEVICE_PIN) == HIGH) {
    // Device is on but should be off; Turn it off
    digitalWrite(CONTROLLED_DEVICE_PIN, LOW);
    #ifdef DEBUG
      Serial.printf("Device: OFF!!!\n");
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
        Serial.printf("Learning started...\n");
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
          Serial.printf("Learning Complete! Paired Device is same as previous!\n\n");
        #endif
      }

      // Do end of learning tasks
      learnStarted = false;
      triggerDeviceLearn = false;

      digitalWrite(LEARN_LED_PIN, LOW);
    }
  }
}

void handleRootGet(HTTPRequest * req, HTTPResponse * res) {
  String page = String(SETTINGS_PAGE);

  std::string value;
  if (req->getParams()->getQueryParameter("message", value)) {
    page.replace("${message}", value.c_str());
  } else {
    page.replace("${message}", "");
  }

  page.replace("${ap_pwd}", settings.getApPwd());
  page.replace("${max_rssi}", String(settings.getCloseRssi()));
  page.replace("${close_rssi}", String(settings.getMaxNearRssi()));
  page.replace("${max_seen}", String(settings.getMaxNotSeenMillis()));
  page.replace("${learn_trigger}", String(settings.getEnableLearnHoldMillis()));
  page.replace("${learn_wait}", String(settings.getLearnWaitMillis()));

  res->setHeader("Content-Type", "text/html");
  res->println(page);
}

void handleRootPost(HTTPRequest * req, HTTPResponse * res) {
  String newApPwd = "";
  int newMaxRssi = -999;
  int newCloseRssi = -999;
  unsigned long newMaxSeenMillis = 0UL;
  unsigned long newLearnTriggerMillis = 0UL;
  unsigned long newLearnWaitMillis = 0UL;

  ResourceParameters * params = req->getParams();
  std::string value;
  
  if (params->getQueryParameter("ap_pwd", value)) {
    newApPwd = String(value.c_str());
    if (params->getQueryParameter("max_rssi", value)) {
      newMaxRssi = std::stoi(value);
      if (params->getQueryParameter("close_rssi", value)) {
        newCloseRssi = std::stoi(value);
        if (params->getQueryParameter("max_seen", value)) {
          newMaxSeenMillis = std::stoul(value);
          if (params->getQueryParameter("learn_trigger", value)) {
            newLearnTriggerMillis = std::stoul(value);
            if (params->getQueryParameter("learn_wait", value)) {
              newLearnWaitMillis = std::stoul(value);

              bool needSave = false;
              bool needReboot = false;
              if (!settings.getApPwd().equals(newApPwd)) {
                needSave = true;
                needReboot = true;
                settings.setApPwd(newApPwd);
              }
              if (settings.getMaxNearRssi() != newMaxRssi) {
                needSave = true;
                settings.setMaxNearRssi(newMaxRssi);
              }
              if (settings.getCloseRssi() != newCloseRssi) {
                needSave = true;
                settings.setCloseRssi(newCloseRssi);
              }
              if (settings.getMaxNotSeenMillis() != newMaxSeenMillis) {
                needSave = true;
                settings.setMaxNotSeenMillis(newMaxSeenMillis);
              }
              if (settings.getEnableLearnHoldMillis() != newLearnTriggerMillis) {
                needSave = true;
                settings.setEnableLearnHoldMillis(newLearnTriggerMillis);
              }
              if (settings.getLearnWaitMillis() != newLearnWaitMillis) {
                needSave = true;
                settings.setLearnWaitMillis(newLearnWaitMillis);
              }

              if (needSave) {
                bool ok = settings.saveSettings();
                #ifdef DEBUG
                  if (ok) {
                    Serial.println("Settings Updated!");
                  } else {
                    Serial.println("Settings update Failed!!!");
                  }
                #endif
                if (needReboot) {
                  #ifdef DEBUG
                    Serial.println("Device rebooting to settings...");
                    delay(10000UL);
                  #endif
                  ESP.restart();
                }
              }
            }
          }
        }
      }
    }
  }

  handleRootGet(req, res);
}

void handle404(HTTPRequest * req, HTTPResponse * res) {
  res->setHeader("Content-Type", "text/html");
  res->println(String(NOT_FOUND_PAGE));
}