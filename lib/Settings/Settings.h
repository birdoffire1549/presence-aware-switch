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

            unsigned long getMaxNotSeenMillis();
            void setMaxNotSeenMillis(unsigned long millis);

            unsigned long getLearnWaitMillis();
            void setLearnWaitMillis(unsigned long millis);

            unsigned long getEnableLearnHoldMillis();
            void setEnableLearnHoldMillis(unsigned long millis);

            String getParedAddress();
            void setParedAddress(String address);

        private:
            struct NVSettings {
                bool             onState                  ;
                int              maxNearRssi              ;
                unsigned long    maxNotSeenMillis         ;
                unsigned long    learnWaitMillis          ;
                unsigned long    enableLearnHoldMillis    ;
                char             pairedAddress    [18]    ;
                char             sentinel         [33]    ; // Holds a 32 MD5 hash + 1
            } nvSettings;

            struct NVSettings factorySettings = {
                false, // <------------------ onState
                -60, // <-------------------- maxNearRssi
                30000UL, // <---------------- maxNotSeenMillis
                10000UL, // <---------------- learnWaitMillis
                5000UL, // <----------------- enableLearnHoldMillis
                "xx:xx:xx:xx:xx:xx", // <---- pairedAddress
                "NA" // <-------------------- sentinel
            };

            void defaultSettings();
            String hashNvSettings(struct NVSettings nvSet);
    };
#endif