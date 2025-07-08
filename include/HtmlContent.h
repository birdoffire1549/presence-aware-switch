#ifndef HtmlContent_h
    #define HtmlContent_h

    #include <WString.h>
    #include <pgmspace.h>

    const char PROGMEM SETTINGS_PAGE[] = {
        "<!DOCTYPE HTML>"
        "<html lang=\"en\">"
            "<head>"
                "<title>Proximity Switch - Settings Page</title>"
                "<style>"
                    "body { background-color: #FFFFFF; color: #000000; }"
                    "h1 { text-align: center; background-color: #5878B0; color: #FFFFFF; border: 3px; border-radius: 15px; }"
                    "h2 { text-align: center; background-color: #58ADB0; color: #FFFFFF; border: 3px; }"
                    "#successful { text-align: center; color: #02CF39; background-color: #000000 }"
                    "#failed { text-align: center; color: #CF0202; background-color: #000000 }"
                    "#wrapper { background-color: #E6EFFF; padding: 20px; margin-left: auto; margin-right: auto; max-width: 700px; box-shadow: 3px 3px 3px #333; }"
                    "#info { font-size: 25px; font-weight: bold; line-height: 150%; }"
                    "button { background-color: #5878B0; color: white; font-size: 16px; padding: 10px 24px; border-radius: 12px; border: 2px solid black; transition-duration: 0.4s; }"
                    "button:hover { background-color: white; color: black; }"
                "</style>"
            "</head>"
            ""    
            "<body>"
                "<div id=\"wrapper\">"
                    "<h1>Proximity Switch</h1>"
                    "<h2>Settings Page</h2>"
                    "Firmware Version: ${version}"
                    "<form action=\"/\" method=\"post\">"
                        "<table>"
                            "<tr><td>AP Password:</td><td><input type=\"password\" id=\"ap_pwd\" name=\"ap_pwd\" value=\"${ap_pwd}\" /></td></tr>"
                            "<tr><td>On Max RSSI:</td><td><input type=\"number\" id=\"max_rssi\" name=\"max_rssi\" min=\"-100\" max=\"0\" step=\"1\" value=\"${max_rssi}\" /></td></tr>"
                            "<tr><td>Close RSSI:</td><td><input type=\"number\" id=\"close_rssi\" name=\"close_rssi\" min=\"-100\" max=\"0\" step=\"1\" value=\"${close_rssi}\" /></td></tr>"
                            "<tr><td>Max Not Seen Millis:</td><td><input type=\"number\" id=\"max_seen\" name=\"max_seen\" min=\"0\" max=\"86400000\" step=\"1\" value=\"${max_seen}\" /></td></tr>"
                            "<tr><td>Learn Trigger Millis:</td><td><input type=\"number\" id=\"learn_trigger\" name=\"learn_trigger\" min=\"0\" max=\"86400000\" step=\"1\" value=\"${learn_trigger}\" /</td></tr>"
                            "<tr><td>Learn Wait Millis:</td><td><input type=\"number\" id=\"learn_wait\" name=\"learn_wait\" min=\"0\" max=\"86400000\" step=\"1\" value=\"${learn_wait}\" /></td></tr>"
                            "<tr><td>Paired Address:</td><td>${pared_address}</td></tr>"
                        "</table>"
                        "<p><button type=\"submit\" name=\"do\" value=\"save_settings\">Update</button></p>"
                    "</form>"
                    "${message}"
                "</div>"
            "</body>"
        "</html>"
    };

    const char PROGMEM SUCCESSFUL[] = {
        "<div id=\"successful\">Settings Update Successful</div>"
    };

    const char PROGMEM FAILED[] = {
        "<div id=\"failed\">Settings Update Failed!</div>"
    };


    const char PROGMEM NOT_FOUND_PAGE[] = {
        "<!DOCTYPE HTML>"
        "<html lang=\"en\">"
            "<head>"
                "<title>404 - Page Not Found</title>"
                "<style>"
                    "body { background-color: #FFFFFF; color: #000000; }"
                    "h1 { text-align: center; background-color: #5878B0; color: #FFFFFF; border: 3px; border-radius: 15px; }"
                    "h2 { text-align: center; background-color: #58ADB0; color: #FFFFFF; border: 3px; }"
                    "#wrapper { background-color: #E6EFFF; padding: 20px; margin-left: auto; margin-right: auto; max-width: 700px; box-shadow: 3px 3px 3px #333; }"
                "</style>"
            "</head>"
            ""    
            "<body>"
                "<div id=\"wrapper\">"
                    "<h1>404 - Page Not Found</h1>"
                    "Ooops... You broke it!"
                "</div>"
            "</body>"
        "</html>"
    };
#endif