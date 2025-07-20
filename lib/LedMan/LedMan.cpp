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
 * @param ledId - The ID of the LED to check caller state for as String.
 * @param caller - The ID of the caller to check LED state for as String.
 */
void LedMan::lockLed(String ledId, String caller) {
    if (locks.count(caller.c_str()) == 0 || locks[caller.c_str()].count(ledId.c_str()) == 0) {
        // An existing lock wasn't found, so create it.
        locks[caller.c_str()][ledId.c_str()] = 1; // Value doesn't matter.
        if (
            callerStates.count(caller.c_str()) == 0
            || callerStates[caller.c_str()].count(ledId.c_str()) == 0
        ) {
            // No caller state for LED so create an off/low state.
            callerStates[caller.c_str()][ledId.c_str()] = LOW;
        }
    }
}

/**
 * Releases a lock on an LED for a given caller.
 * 
 * @param ledId - The ID of the LED to check caller state for as String.
 * @param caller - The ID of the caller to check LED state for as String.
 */
void LedMan::releaseLed(String ledId, String caller) {
    if (locks.count(caller.c_str()) > 0 && locks[caller.c_str()].count(ledId.c_str()) > 0) {
        // Locked on specified LED.
        locks[caller.c_str()].erase(ledId.c_str());
        if (
            callerStates.count(caller.c_str()) > 0 
            && callerStates[caller.c_str()].count(ledId.c_str()) > 0 
            && callerStates[caller.c_str()][ledId.c_str()] == LOW
        ) {
            // Erase LOW state because lock has been released.
            callerStates[caller.c_str()].erase(ledId.c_str());
        }
    }
}

/**
 * Sets the LED state for a given caller to on/high.
 * 
 * @param ledId - The ID of the LED to check caller state for as String.
 * @param caller - The ID of the caller to check LED state for as String.
 */
void LedMan::ledOn(String ledId, String caller) {
    callerStates[caller.c_str()][ledId.c_str()] = HIGH; 
}

/**
 * Sets the LED state for a given caller to off/low.
 * If the caller is locked on the LED then a LOW state
 * is created/maintained, otherwise the LED state is
 * removed so any other caller may turn the LED on if
 * so desired.
 * 
 * @param ledId - The ID of the LED to check caller state for as String.
 * @param caller - The ID of the caller to check LED state for as String.
 */
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
            // Caller exists so attempt to erase the state for the LED
            callerStates[caller.c_str()].erase(ledId.c_str());
        }
    }
}

/**
 * This function must be called repeatedly and ideally as quickly
 * as possible. It contains the code to manage the state of the 
 * device's LEDs. Ideally a call to this would go in the 
 * firmware's main loop method.
 */
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
                    // Caller priority is higher (less) or same to referenced one
                    lastPriority = priorities[caller.c_str()];
                    std::map<std::string/*LedId*/, int/*HighLow*/> ledStates = callerState.second;
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

/**
 * Toggles the LED state for a given LED and Caller.
 * 
 * @param ledId - The ID of the LED to check caller state for as String.
 * @param caller - The ID of the caller to check LED state for as String.
 */
void LedMan::ledToggle(String ledId, String caller) {
    if (currentState(ledId, caller) == HIGH) {
        ledOff(ledId, caller);
    } else {
        ledOn(ledId, caller);
    }
}

/**
 * Returns the current on/off state for a given caller on a
 * given LED. This may or not reflect the LED's actual state.
 * 
 * @param ledId - The ID of the LED to check caller state for as String.
 * @param caller - The ID of the caller to check LED state for as String.
 * 
 * @return Returns the High/Low state as int.
 */
int LedMan::currentState(String ledId, String caller) {
    if (
        callerStates.count(caller.c_str()) > 0
        && callerStates[caller.c_str()].count(ledId.c_str()) > 0
    ) {
        // Return whatever we found
        return callerStates[caller.c_str()][ledId.c_str()];
    }
    // Finding nothing is same as LOW

    return LOW;
}