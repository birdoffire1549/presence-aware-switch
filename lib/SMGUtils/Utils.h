#ifndef Utils_h
    #define Utils_h

    #include <Settings.h>

    class Utils {
        private:

        public:
            static String hashString(String string);
            static String genDeviceIdFromMacAddr(String macAddress);
            static String userFriendlyElapsedTime(unsigned long elapsedMillis);
    };

#endif