#ifndef Settings_h
    #define Settings_h

    #include <WString.h>
    #include <EEPROM.h>
    #include <MD5Builder.h>

    class Settings {
        public:
            Settings();

            bool loadSettings();
            bool saveSettings();
            bool factoryDefault();

            // Getters and Setters
            bool isOnState();
            void setOnState(bool onState);

            int getMaxNearRssi();
            void setMaxNearRssi(int rssi);

            int getCloseRssi();
            void setCloseRssi(int rssi);

            unsigned long getStartups();
            unsigned long getLastStartMillis();
            void logStartup();

            unsigned long getMaxNotSeenMillis();
            void setMaxNotSeenMillis(unsigned long millis);

            unsigned long getLearnDurationMillis();
            void setLearnDurationMillis(unsigned long millis);

            unsigned long getTriggerLearnMillis();
            void setTriggerLearnMillis(unsigned long millis);

            unsigned long getTriggerFactoryMillis();
            void setTriggerFactoryMillis(unsigned long millis);

            unsigned long getTriggerWiFiOnMillis();
            void setTriggerWiFiOnMillis(unsigned long millis);

            unsigned long getTriggerWiFiOffMillis();
            void setTriggerWiFiOffMillis(unsigned long millis);

            String getParedAddress();
            void setParedAddress(String address);

            String getApPwd();
            void setApPwd(String apPwd);

        private:
            struct NVSettings {
                int              maxNearRssi              ;
                int              closeRssi                ;
                unsigned long    startups                 ;
                unsigned long    lastStartMillis          ;
                unsigned long    maxNotSeenMillis         ;
                unsigned long    learnDurationMillis      ;
                unsigned long    triggerLearnMillis       ;
                unsigned long    triggerFactoryMillis     ;
                unsigned long    triggerWiFiOnMillis      ;
                unsigned long    triggerWiFiOffMillis     ;
                char             pairedAddress    [18]    ;
                char             apPwd            [64]    ;
                char             sentinel         [33]    ; // Holds a 32 MD5 hash + 1
            } nvSettings;

            struct NVSettings factorySettings = {
                -80, // <-------------------- maxNearRssi
                -50, // <-------------------- closeRssi
                0UL, // <-------------------- startups
                0UL, // <-------------------- lastStartMillis
                60000UL, // <---------------- maxNotSeenMillis
                10000UL, // <---------------- learnDurationMillis
                5000UL, // <----------------- triggerLearnMillis
                30000UL, // <----------------- triggerFactoryMillis
                10000UL, // <----------------- triggerWiFiOnMillis
                5000UL, // <----------------- triggerWiFiOffMillis
                "xx:xx:xx:xx:xx:xx", // <---- pairedAddress
                "P@ssw0rd123", // <---------- apPwd
                "NA" // <-------------------- sentinel
            };

            struct VSettings {
                bool             onState                  ;
            } vSettings = {
                false, // <------------------ onState
            };

            void defaultSettings();
            String hashNvSettings(struct NVSettings nvSet);
    };
#endif