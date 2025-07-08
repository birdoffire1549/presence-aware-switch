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
        std::map<std::string/*LedId*/, int/*PinNumber*/> regLeds;

        std::map<std::string/*Caller*/, int/*Priority*/> priorities;

        std::map<std::string/*Caller*/, std::map<std::string/*LedId*/, int/*Junk*/>> locks;
        std::map<std::string/*Caller*/, std::map<std::string/*LedId*/, int/*CallerState*/>> callerStates;
    };
#endif