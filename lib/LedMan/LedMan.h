/*
    LedMan.h
    This is the header file for the LedMan Class.

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
#ifndef LedMan_h
    #define LedMan_h    
    
    #include <Arduino.h>
    #include <WString.h>
    #include <map>
    #include <algorithm>
    
    class LedMan {
    public:
        void addLed(int ledPin, String ledId);
        void setCallerPriority(String caller, int priority);
        void lockLed(String ledId, String caller);
        void releaseLed(String ledId, String caller);
        void ledOn(String ledId, String caller);
        void ledOff(String ledId, String caller);
        void ledToggle(String ledId, String caller);
        int currentState(String ledId, String caller);
        void loop();

    private:
        std::map<std::string/*LedId*/, int/*PinNumber*/> registeredLeds;
        std::map<std::string/*Caller*/, int/*Priority*/> priorities;
        std::map<std::string/*Caller*/, std::map<std::string/*LedId*/, int/*Junk*/>> locks;
        std::map<std::string/*Caller*/, std::map<std::string/*LedId*/, int/*CallerState*/>> callerStates;
    };
#endif