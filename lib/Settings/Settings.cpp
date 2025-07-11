/*
  Settings - A class to contain, maintain, store and retreive settings needed
  by the application. This Class object is intented to be the sole manager of 
  data used throughout the applicaiton. It handles storing both volitile and 
  non-volatile data, where by definition the non-volitile data is persisted
  in flash memory and lives beyond the running life of the software and the 
  volatile data is lost and defaulted each time the software runs.

  Written by: ... Scott Griffis
  Date: ......... 06/15/2025
*/

#include <Settings.h>

Settings::Settings() {
    defaultSettings();
}

/**
 * Performs a factory default on the information maintained by this class
 * where that the data is first set to its factory default settings then
 * it is persisted to flash.
 * 
 * @return Returns true if successful saving defaulted settings otherwise
 * returns false as bool.
*/
bool Settings::factoryDefault() {
    defaultSettings();
    bool ok = saveSettings();

    return ok;
}

/**
 * Used to save or persist the current value of the non-volatile settings
 * into flash memory.
 *
 * @return Returns a true if save was successful otherwise a false as bool.
*/
bool Settings::saveSettings() {
    strcpy(nvSettings.sentinel, hashNvSettings(nvSettings).c_str()); // Ensure accurate Sentinel Value.
    EEPROM.begin(sizeof(NVSettings));
    EEPROM.put(0, nvSettings);    
    bool ok = EEPROM.commit();
    EEPROM.end();
    
    return ok;
}

/**
 * Used to load the settings from flash memory.
 * After the settings are loaded from flash memory the sentinel value is 
 * checked to ensure the integrity of the loaded data. If the sentinel 
 * value is wrong then the contents of the memory are deemed invalid and
 * the memory is wiped and then a factory default is instead performed.
 * 
 * @return Returns true if data was loaded from memory and the sentinel 
 * value was valid.
*/
bool Settings::loadSettings() {
    bool ok = false;
    // Setup EEPROM for loading and saving...
    EEPROM.begin(sizeof(NVSettings));

    // Persist default settings or load settings...
    delay(15);

    /* Load from EEPROM if applicable... */
    EEPROM.get(0, nvSettings);
    if (strcmp(nvSettings.sentinel, hashNvSettings(nvSettings).c_str()) != 0) { // Memory is corrupt...
        factoryDefault();
    } else { // Memory seems ok...
        ok = true;
    }
    
    EEPROM.end();

    return ok;
}

bool Settings::isOnState() { return vSettings.onState; }
void Settings::setOnState(bool onState) { vSettings.onState = onState; }

int Settings::getMaxNearRssi() { return nvSettings.maxNearRssi; }
void Settings::setMaxNearRssi(int rssi) { nvSettings.maxNearRssi = rssi; }

int Settings::getCloseRssi() { return nvSettings.closeRssi; }
void Settings::setCloseRssi(int rssi) { nvSettings.closeRssi = rssi; }

unsigned long Settings::getMaxNotSeenMillis() { return nvSettings.maxNotSeenMillis; }
void Settings::setMaxNotSeenMillis(unsigned long millis) { nvSettings.maxNotSeenMillis = millis; }

unsigned long Settings::getLearnWaitMillis() { return nvSettings.learnWaitMillis; }
void Settings::setLearnWaitMillis(unsigned long millis) { nvSettings.learnWaitMillis = millis; }

unsigned long Settings::getEnableLearnHoldMillis() { return nvSettings.enableLearnHoldMillis; }
void Settings::setEnableLearnHoldMillis(unsigned long millis) { nvSettings.enableLearnHoldMillis = millis; }

String Settings::getParedAddress() { return String(nvSettings.pairedAddress); }
void Settings::setParedAddress(String address) { strcpy(nvSettings.pairedAddress, address.c_str()); }

String Settings::getApPwd() { return String(nvSettings.apPwd); }
void Settings::setApPwd(String apPwd) { strcpy(nvSettings.apPwd, apPwd.c_str()); }

unsigned long Settings::getStartups() { return nvSettings.startups;}
unsigned long Settings::getLastStartMillis() { return nvSettings.lastStartMillis; }

void Settings::logStartup() {
    nvSettings.startups = nvSettings.startups + 1UL;
    nvSettings.lastStartMillis = millis();
    saveSettings();
}

/*
=================================================================
Private Functions BELOW
=================================================================
*/

/**
 * #### PRIVATE ####
 * This function is used to set or reset all settings to 
 * factory default values but does not persist the value 
 * changes to flash.
*/
void Settings::defaultSettings() {
    // Default the settings...
    nvSettings.startups = factorySettings.startups;
    nvSettings.lastStartMillis = factorySettings.lastStartMillis;
    nvSettings.maxNearRssi = factorySettings.maxNearRssi;
    nvSettings.closeRssi = factorySettings.closeRssi;
    nvSettings.maxNotSeenMillis = factorySettings.maxNotSeenMillis;
    nvSettings.learnWaitMillis = factorySettings.learnWaitMillis;
    nvSettings.enableLearnHoldMillis = factorySettings.enableLearnHoldMillis;
    strcpy(nvSettings.pairedAddress, factorySettings.pairedAddress);
    strcpy(nvSettings.apPwd, factorySettings.apPwd);
}

/**
 * #### PRIVATE ####
 * Used to provide a hash of the given NonVolatileSettings.
 * 
 * @param nvSet An instance of NonVolatileSettings to calculate a hash for.
 * 
 * @return Returns the calculated hash value as String.
*/
String Settings::hashNvSettings(NVSettings nvSet) {
    String content = "";
    content = content + String(nvSet.maxNearRssi);
    content = content + String(nvSet.closeRssi);
    content = content + String(nvSet.maxNotSeenMillis);
    content = content + String(nvSet.learnWaitMillis);
    content = content + String(nvSet.enableLearnHoldMillis);
    content = content + nvSet.pairedAddress;
    content = content + nvSet.apPwd;
    
    MD5Builder builder = MD5Builder();
    builder.begin();
    builder.add(content);
    builder.calculate();

    return builder.toString();
}

