#include "Utils.h"

/**
 * Function used to perform a MD5 Hash on a given string
 * the result is the MD5 Hash.
 * 
 * @param string The string to hash as String.
 * 
 * @return Returns the generated MD5 Hash as String.
*/
String Utils::hashString(String string) {
    MD5Builder builder = MD5Builder();
    builder.begin();
    builder.add(string);
    builder.calculate();
    
    return builder.toString();
}

/**
 * Generates a six character Device ID based on the
 * given macAddress.
 * 
 * @param macAddress The device's MAC Address as String.
 * 
 * @return Returns a six digit Device ID as String.
*/
String Utils::genDeviceIdFromMacAddr(String macAddress) {
    String result = hashString(macAddress);
    int len = result.length();
    if (len > 6) {
        result = result.substring((len - 6), len);
    }
    result.toUpperCase();

    return result;
}

/**
 * Used to generate a user friendly human readable string which is
 * capable of telling the number of Weeks, Days, Hours, Mins, Secs of 
 * a given elapsed time in milliseconds.
 * 
 * @param elapsedMillis - The elapsed milliseconds as unsigned long value.
 * 
 * @return Returns a user friendly String representation of the elapsed time.
 */
String Utils::userFriendlyElapsedTime(unsigned long elapsedMillis) {
    static const unsigned long minMillis = 60000UL;
    static const unsigned long hourMillis = minMillis * 60UL;
    static const unsigned long dayMillis = hourMillis * 24UL;
    static const unsigned long weekMillis = dayMillis * 7UL;

    String result = "";
    unsigned long timeLeftMillis = elapsedMillis;
    
    unsigned long refVal = timeLeftMillis / weekMillis;
    if (refVal > 0) {
        result += (String(refVal) + " Week, ");
        timeLeftMillis -= (refVal * weekMillis);
    }

    refVal  = timeLeftMillis / dayMillis;
    if (refVal > 0 || !result.isEmpty()) {
        result += (String(refVal) + " Day, ");
        timeLeftMillis -= (refVal * dayMillis);
    }

    refVal  = timeLeftMillis / hourMillis;
    if (refVal > 0 || !result.isEmpty()) {
        result += (String(refVal) + " Hour, ");
        timeLeftMillis -= (refVal * hourMillis);
    }

    refVal  = timeLeftMillis / minMillis;
    if (refVal > 0 || !result.isEmpty()) {
        result += (String(refVal) + " Min, ");
        timeLeftMillis -= (refVal * minMillis);
    }

    refVal  = timeLeftMillis / 1000UL;
    if (refVal > 0) {
        result += (String(refVal) + " Sec");
        timeLeftMillis -= (refVal * 1000UL);
    }

    return result;
}