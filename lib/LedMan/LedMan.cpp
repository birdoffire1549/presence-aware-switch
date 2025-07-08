#include <LedMan.h>

void LedMan::addLed(int ledPin, String ledId) {
    regLeds[ledId.c_str()] = ledPin;
}

void LedMan::setCallerPriority(String caller, int priority) {
    priorities[caller.c_str()] = priority;
}

void LedMan::lockLed(String ledId, String caller) {
    if (locks.count(caller.c_str()) == 0 || locks[caller.c_str()].count(ledId.c_str()) == 0) {
        locks[caller.c_str()][ledId.c_str()] = 1;
        if (
            callerStates.count(caller.c_str()) == 0
            || callerStates[caller.c_str()].count(ledId.c_str()) == 0
        ) {
            callerStates[caller.c_str()][ledId.c_str()] = LOW;
        }
    }
}

void LedMan::releaseLed(String ledId, String caller) {
    if (locks.count(caller.c_str()) > 0 && locks[caller.c_str()].count(ledId.c_str()) > 0) {
        locks.erase(ledId.c_str());
        if (
            callerStates.count(caller.c_str()) > 0 
            && callerStates[caller.c_str()].count(ledId.c_str()) > 0 
            && callerStates[caller.c_str()][ledId.c_str()] == LOW
        ) {
            callerStates[caller.c_str()].erase(ledId.c_str());
        }
    }
}

void LedMan::ledOn(String ledId, String caller) {
    callerStates[caller.c_str()][ledId.c_str()] = HIGH; 
}

void LedMan::ledOff(String ledId, String caller) {
    if (
        locks.count(caller.c_str()) > 0 
        && locks[caller.c_str()].count(ledId.c_str()) > 0
    ) {
        // If locked on LED then we need to set a LOW state
        callerStates[caller.c_str()][ledId.c_str()] = LOW;
    } else {
        // If not locked then delete state for LED for LOW
        if (callerStates.count(caller.c_str()) > 0) {
            callerStates[caller.c_str()].erase(ledId.c_str());
        }
    }
}

void LedMan::loop() {
    for (const auto& regLed : regLeds) {
        // Determine current status for each LED
        int ledPin = regLed.second;
        std::string ledId = regLed.first;

        int calcState = LOW;
        int lastPriority = INT_MAX;

        for (const auto& callerState : callerStates) {
            if (callerState.second.count(ledId.c_str()) > 0) {
                // Caller state has a state for current LED
                std::string caller = callerState.first;
                if (
                    lastPriority == INT_MAX
                    || (
                        priorities.count(caller.c_str()) > 0
                        && priorities[caller.c_str()] <= lastPriority
                    )
                ) {
                    lastPriority = priorities[caller.c_str()];
                    std::map<std::string, int> ledStates = callerState.second;
                    calcState = ledStates[ledId];
                }
            }
        }

        // Set LED to desired State
        if (digitalRead(ledPin) != calcState) {
            digitalWrite(ledPin, calcState);
        }
    }
}

void LedMan::ledToggle(String ledId, String caller) {
    if (currentState(ledId, caller) == HIGH) {
        ledOff(ledId, caller);
    } else {
        ledOn(ledId, caller);
    }
}

int LedMan::currentState(String ledId, String caller) {
    if (callerStates.count(caller.c_str()) > 0) {
        if (callerStates[caller.c_str()].count(ledId.c_str()) > 0) {
            // Return whatever we found
            return callerStates[caller.c_str()][ledId.c_str()];
        }
    }
    // Finding nothing is same as LOW

    return LOW;
}