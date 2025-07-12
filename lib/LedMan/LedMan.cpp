/*
    LedMan.cpp
    This is the code file for the LedMan Class.

    The purpose of this class is to act as the sole master of device LEDs. All other parts of the
    firmware must interact with the LEDs through this class. This allows for LEDs to have a kind
    of overloaded functionality where they can be driven by different processes with some processes
    having a higher priority than others. This way every process can use an LED when they wish without
    having to worry about the current and final on/off state of the LED.

    For example, say that process 'A' desires for an LED to remain lit, but process 'B' which has a 
    higher priority wishes to flash the LED. Using this class, process 'B' can lock the LED for use
    and flash it as dessired. When finished process 'B' can set the LED to off and release the lock.
    At that point the state of process 'A' would take over and the LED would go back to remaining lit.

    Written by: ... Scott Griffis
    Date: ......... 07/07/2025
*/

#include <LedMan.h>

/**
 * Used to Register an LED with this class so that it can be 
 * controlled by users of this class.
 * 
 * @param ledPin - This is the device pin for the LED as int.
 */
void LedMan::addLed(int ledPin, String ledId) {
    registeredLeds[ledId.c_str()] = ledPin;
}

/**
 * Used to set the priority for a caller function/process.
 * The lower the priority value the more priority the caller
 * process has.
 * 
 * @param caller - The ID of the caller as String.
 * @param priority - The priority of the caller as int.
 */
void LedMan::setCallerPriority(String caller, int priority) {
    priorities[caller.c_str()] = priority;
}

/**
 * Used to obtain a lock for an LED. 
 * This isn't an exclusive access kind of lock, it is more of a lock that 
 * says both on and off states of the LED are important to this process/caller
 * so as long as it has the priority to do so it may force the LED to be off 
 * even if a lower priority processs/caller wants it on. Without a lock, when 
 * a process sets the led to off another lower priority process can turn it on.
 * 
 * @param ledId - The String ID for the LED.
 * @param caller - The String ID for the Caller.
 */
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
        // If locked on specified LED
        locks[caller.c_str()].erase(ledId.c_str());
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
    for (const auto& regLed : registeredLeds) {
        // Determine current status for each LED
        int ledPin = regLed.second;
        std::string ledId = regLed.first;

        int calcState = LOW;
        int lastPriority = INT_MAX;

        for (const auto& callerState : callerStates) {
            // Iterate caller states to see what each wants state to be
            if (callerState.second.count(ledId.c_str()) > 0) {
                // Caller has a state for current LED
                std::string caller = callerState.first;
                if (
                    lastPriority == INT_MAX
                    || (
                        priorities.count(caller.c_str()) > 0
                        && priorities[caller.c_str()] <= lastPriority
                    )
                ) {
                    // Caller priority is greater or same to referenced one
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