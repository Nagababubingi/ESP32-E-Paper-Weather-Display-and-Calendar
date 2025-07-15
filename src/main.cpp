#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <epd.h>

// WiFi Credentials
const char* ssid = "Babu";
const char* password = "Babu123456789";

// API Key
const char* apiKey = "20dd189388362a7e163c6526ca6ed9da";

// API URLs
const char* newsAPI = "https://api.thenewsapi.com/v1/news/top?api_token=2y7F00idNjzuNte7ryz0QzpxCZWCXagLsLuEvEag&locale=in";
const char* poemAPI = "https://poetrydb.org/random";
const char* thoughtAPI = "https://zenquotes.io/api/today";
const char* geoAPI = "http://ip-api.com/json";

// NTP Client Setup
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 19800, 60000);

// GPIO Pins
const int wake_up = 5;
const int reset = 4;
const int switchPin = 15;
volatile int displayMode = 0; // 0: Calendar+Weather, 1: News, 2: Poem, 3: Thought
volatile bool modeChanged = false;

// Days and Months
const char* days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
const char* months[] = {"January", "February", "March", "April", "May", "June", 
                        "July", "August", "September", "October", "November", "December"};

// Global Variables
String city = "Unknown";
String country = "Unknown";
float temp = 0.0, feels_like = 0.0;
int humidity = 0, visibility = 0;

// Poem display state
String poemLines[20]; // Array to store poem lines
int totalPoemLines = 0;
int currentPoemLine = 0;
unsigned long poemDisplayTime = 0;
const unsigned long poemDisplayInterval = 60000; // 1 minute

// Function Declarations
void draw_display1(int year, int month, int day, float temp, int humidity, String city, float feels_like, int visibility);
void draw_display();
void fetch_weather();
void get_location();
int get_first_day_of_month(int year, int month);
void draw_calendar(int year, int month, int day);
float get_battery_percentage();
void ensureWiFiConnected();
void fetch_news();
void fetch_poem();
void fetch_thought();
void fetch_weather_and_calendar();
void IRAM_ATTR handleSwitch();
void displayError(const char* message);
void display_current_poem_part();

void ensureWiFiConnected() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("Reconnecting to WiFi...");
        WiFi.disconnect();
        WiFi.begin(ssid, password);
        
        int attempts = 0;
        while (WiFi.status() != WL_CONNECTED && attempts < 20) {
            delay(500);
            Serial.print(".");
            attempts++;
        }
        
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("Failed to reconnect to WiFi");
            displayError("WiFi Connection Failed");
        } else {
            Serial.println("Reconnected!");
        }
    }
}

void IRAM_ATTR handleSwitch() {
    displayMode = (displayMode + 1) % 4;
    modeChanged = true;
}

void get_location() {
    HTTPClient http;
    http.begin(geoAPI);
    http.setTimeout(10000);
    
    int httpResponseCode = http.GET();
    if (httpResponseCode == 200) {
        String payload = http.getString();
        
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, payload);
        if (error) {
            Serial.print("deserializeJson() failed: ");
            Serial.println(error.c_str());
            city = "Unknown";
            country = "Unknown";
        } else {
            city = doc["city"].as<String>();
            country = doc["countryCode"].as<String>();
            Serial.println("Detected Location: " + city + ", " + country);
        }
    } else {
        Serial.println("Failed to get location");
        city = "Unknown";
        country = "Unknown";
    }
    http.end();
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    
    Serial.println("Initializing...");
    
    epd_init(wake_up, reset);
    epd_wakeup(wake_up);
    epd_set_memory(MEM_NAND);
    epd_clear();
    epd_disp_string("Connecting to WiFi...", 100, 100);
    epd_udpate();
    
    WiFi.begin(ssid, password);
    unsigned long startTime = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startTime < 30000) {
        delay(500);
        Serial.print(".");
    }
    
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("\nWiFi Connection Failed");
        displayError("WiFi Connection Failed");
        return;
    }
    
    Serial.println("\nConnected!");
    timeClient.begin();
    get_location();
    
    pinMode(switchPin, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(switchPin), handleSwitch, FALLING);
    
    draw_display();
}

void loop() {
    if (modeChanged) {
        modeChanged = false;
        draw_display();
    }
    
    // Handle poem rotation if in poem mode
    if (displayMode == 2 && totalPoemLines > 0) {
        if (millis() - poemDisplayTime >= poemDisplayInterval) {
            currentPoemLine += 10; // Move to next 10 lines
            if (currentPoemLine >= totalPoemLines) {
                currentPoemLine = 0; // Loop back to start
            }
            display_current_poem_part();
            poemDisplayTime = millis();
        }
    }
    
    // Regular update check
    static unsigned long lastUpdate = 0;
    if (millis() - lastUpdate >= 1800000 || lastUpdate == 0) {
        lastUpdate = millis();
        ensureWiFiConnected();
        draw_display();
    }
    
    delay(1000);
}

void draw_display() {
    epd_clear();
    
    switch (displayMode) {
        case 0: fetch_weather_and_calendar(); break;
        case 1: fetch_news(); break;
        case 2: fetch_poem(); break;
        case 3: fetch_thought(); break;
        default: displayMode = 0; fetch_weather_and_calendar();
    }
    
    epd_udpate();
}

void display_current_poem_part() {
    epd_clear();
    epd_set_color(BLACK, WHITE);
    epd_set_en_font(ASCII32);
    
    int y = 50;
    int linesToShow = min(10, totalPoemLines - currentPoemLine); // Show up to 10 lines
    
    for (int i = 0; i < linesToShow; i++) {
        epd_disp_string(poemLines[currentPoemLine + i].c_str(), 50, y);
        y += 40;
    }
    
    // Show progress indicator if poem continues
    if (currentPoemLine + linesToShow < totalPoemLines) {
        epd_disp_string("... more ...", 350, y + 20);
    }
    
    // Show current part indicator
    char partInfo[20];
    int totalParts = (totalPoemLines + 9) / 10; // Round up division
    int currentPart = (currentPoemLine / 10) + 1;
    sprintf(partInfo, "Part %d/%d", currentPart, totalParts);
    epd_disp_string(partInfo, 600, 20);
    
    epd_udpate();
}

void fetch_poem() {
    HTTPClient http;
    http.begin(poemAPI);
    http.setTimeout(10000);
    
    int httpResponseCode = http.GET();
    if (httpResponseCode == 200) {
        String payload = http.getString();
        
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, payload);
        if (error) {
            Serial.print("deserializeJson() failed: ");
            Serial.println(error.c_str());
            displayError("Poem Parse Error");
            return;
        }
        
        if (!doc.isNull() && doc.size() > 0) {
            // Reset poem state
            totalPoemLines = 0;
            currentPoemLine = 0;
            
            // Store title and author
            poemLines[totalPoemLines++] = doc[0]["title"].as<String>();
            poemLines[totalPoemLines++] = "by " + doc[0]["author"].as<String>();
            poemLines[totalPoemLines++] = ""; // Empty line for spacing
            
            // Store poem lines
            JsonArray lines = doc[0]["lines"].as<JsonArray>();
            for (JsonVariant line : lines) {
                if (totalPoemLines < 20) { // Prevent array overflow
                    poemLines[totalPoemLines++] = line.as<String>();
                }
            }
            
            // Display first part
            display_current_poem_part();
            poemDisplayTime = millis();
        } else {
            displayError("No Poem Found");
        }
    } else {
        Serial.println("Failed to fetch poem");
        displayError("Poem Fetch Failed");
    }
    http.end();
}

void fetch_weather_and_calendar() {
    fetch_weather();
    timeClient.update();
    time_t now = timeClient.getEpochTime();
    struct tm* timeinfo = localtime(&now);
    int year = timeinfo->tm_year + 1900;
    int month = timeinfo->tm_mon;
    int day = timeinfo->tm_mday;

    draw_display1(year, month, day, temp, humidity, city, feels_like, visibility);
}

void fetch_weather() {
    if (city == "Unknown" || country == "Unknown") {
        Serial.println("Cannot fetch weather - location unknown");
        return;
    }

    String weatherURL = "http://api.openweathermap.org/data/2.5/weather?q=" + city + "," + country + "&units=metric&appid=" + String(apiKey);

    HTTPClient http;
    http.begin(weatherURL);
    http.setTimeout(10000);
    
    int httpResponseCode = http.GET();
    if (httpResponseCode == 200) {
        String payload = http.getString();

        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, payload);
        if (error) {
            Serial.print("deserializeJson() failed: ");
            Serial.println(error.c_str());
            return;
        }

        temp = doc["main"]["temp"];
        humidity = doc["main"]["humidity"];
        feels_like = doc["main"]["feels_like"];
        visibility = doc["visibility"];
    } else {
        Serial.println("Failed to get weather data");
    }
    http.end();
}

void draw_display1(int year, int month, int day, float temp, int humidity, String city, float feels_like, int visibility) {
    epd_clear();

    // Top Section
    epd_set_color(BLACK, WHITE);
    epd_set_en_font(ASCII32);
    
    String ipStr = "IP: " + WiFi.localIP().toString();
    if (ipStr.length() > 15) ipStr = ipStr.substring(0, 15) + "...";
    epd_disp_string(ipStr.c_str(), 570, 20);

    char battery_str[32];
    sprintf(battery_str, "Bat:%.0f%%", get_battery_percentage());
    epd_disp_string(battery_str, 50, 20);

    String cityUpper = city;
    cityUpper.toUpperCase();
    if (cityUpper.length() > 15) cityUpper = cityUpper.substring(0, 15) + "...";
    epd_disp_string(cityUpper.c_str(), 320, 20);

    // Calendar Section
    draw_calendar(year, month, day);

    // Weather Section
    epd_set_color(WHITE, DARK_GRAY);
    epd_set_en_font(ASCII48);
    epd_disp_string("Today's Weather", 250, 450);

    epd_set_en_font(ASCII32);
    epd_set_color(BLACK, GRAY);
    epd_disp_string("Temp:", 50, 520);
    epd_disp_string("Humidity:", 50, 570);
    epd_disp_string("Feels Like:", 450, 520);
    epd_disp_string("Visibility:", 450, 570);

    epd_set_color(BLACK, WHITE);
    char temp_str[32], humidity_str[32], feels_like_str[32], visibility_str[32];
    sprintf(temp_str, "%.1f C", temp);
    sprintf(humidity_str, "%d%%", humidity);
    sprintf(feels_like_str, "%.1f C", feels_like);
    sprintf(visibility_str, "%d m", visibility/1000);

    epd_disp_string(temp_str, 150, 520);
    epd_disp_string(humidity_str, 180, 570);
    epd_disp_string(feels_like_str, 580, 520);
    epd_disp_string(visibility_str, 580, 570);
}

void draw_calendar(int year, int month, int day) {
    epd_set_en_font(ASCII48);
    epd_set_color(WHITE, DARK_GRAY);
    
    char date_header[64];
    sprintf(date_header, "%s %d", months[month], year);
    epd_disp_string(date_header, 250, 80);

    epd_set_en_font(ASCII32);
    epd_set_color(BLACK, GRAY);
    for (int i = 0; i < 7; i++) {
        epd_disp_string(days[i], 50 + i * 100, 140);
    }

    int first_day = get_first_day_of_month(year, month);
    int days_in_month = 31;
    if (month == 1) {
        days_in_month = (((year % 4 == 0) && (year % 100 != 0)) || (year % 400 == 0)) ? 29 : 28;
    } else if (month == 3 || month == 5 || month == 8 || month == 10) {
        days_in_month = 30;
    }

    int x = 50 + first_day * 100;
    int y = 180;
    for (int d = 1; d <= days_in_month; d++) {
        char date_str[4];
        sprintf(date_str, "%d", d);

        if (d == day) {
            epd_set_color(BLACK, WHITE);
            epd_fill_rect(x-5, y-5, x+35, y+35);
            epd_set_color(WHITE, BLACK);
        } else {
            epd_set_color(BLACK, WHITE);
        }
        
        epd_disp_string(date_str, x, y);

        x += 100;
        if (x >= 750) {
            x = 50;
            y += 50;
        }
    }
}

int get_first_day_of_month(int year, int month) {
    struct tm timeinfo = {0};
    timeinfo.tm_year = year - 1900;
    timeinfo.tm_mon = month;
    timeinfo.tm_mday = 1;
    timeinfo.tm_hour = 12;
    time_t first_day = mktime(&timeinfo);
    return localtime(&first_day)->tm_wday;
}

float get_battery_percentage() {
    float voltage = analogRead(35) / 4096.0 * 7.46;
    voltage = constrain(voltage, 3.5, 4.2);
    return (voltage - 3.5) * 100 / 0.7;
}

void displayError(const char* message) {
    epd_clear();
    epd_set_color(BLACK, WHITE);
    epd_set_en_font(ASCII48);
    epd_disp_string("ERROR", 350, 200);
    epd_set_en_font(ASCII32);
    epd_disp_string(message, 100, 300);
    epd_udpate();
}

void fetch_news() {
    HTTPClient http;
    http.begin(newsAPI);
    http.setTimeout(10000);
    
    int httpResponseCode = http.GET();
    if (httpResponseCode == 200) {
        String payload = http.getString();
        
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, payload);
        if (error) {
            Serial.print("deserializeJson() failed: ");
            Serial.println(error.c_str());
            displayError("News Parse Error");
            return;
        }
        
        JsonArray articles = doc["data"].as<JsonArray>();
        if (!articles.isNull() && articles.size() > 0) {
            String headline = articles[0]["title"].as<String>();
            epd_clear();
            epd_set_color(BLACK, WHITE);
            epd_set_en_font(ASCII32);
            
            int y = 100;
            int maxLines = 10;
            String remaining = headline;
            
            while (remaining.length() > 0 && y < 700 && maxLines-- > 0) {
                int spacePos = remaining.indexOf(' ', 30);
                if (spacePos == -1 || spacePos > 50) spacePos = min(50, (int)remaining.length());
                
                String line = remaining.substring(0, spacePos);
                epd_disp_string(line.c_str(), 50, y);
                y += 40;
                
                if (spacePos < remaining.length()) {
                    remaining = remaining.substring(spacePos + 1);
                } else {
                    remaining = "";
                }
            }
        } else {
            displayError("No News Found");
        }
    } else {
        Serial.println("Failed to fetch news");
        displayError("News Fetch Failed");
    }
    http.end();
}

void fetch_thought() {
    HTTPClient http;
    http.begin(thoughtAPI);
    http.setTimeout(10000);
    
    int httpResponseCode = http.GET();
    if (httpResponseCode == 200) {
        String payload = http.getString();
        
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, payload);
        if (error) {
            Serial.print("deserializeJson() failed: ");
            Serial.println(error.c_str());
            displayError("Quote Parse Error");
            return;
        }
        
        if (!doc.isNull() && doc.size() > 0) {
            String quote = doc[0]["q"].as<String>();
            String author = doc[0]["a"].as<String>();
            
            epd_clear();
            epd_set_color(BLACK, WHITE);
            epd_set_en_font(ASCII32);
            
            int y = 100;
            int maxLines = 10;
            String remaining = quote;
            
            while (remaining.length() > 0 && y < 600 && maxLines-- > 0) {
                int spacePos = remaining.indexOf(' ', 30);
                if (spacePos == -1 || spacePos > 50) spacePos = min(50, (int)remaining.length());
                
                String line = remaining.substring(0, spacePos);
                epd_disp_string(line.c_str(), 50, y);
                y += 40;
                
                if (spacePos < remaining.length()) {
                    remaining = remaining.substring(spacePos + 1);
                } else {
                    remaining = "";
                }
            }
            
            epd_disp_string(("- " + author).c_str(), 50, y + 40);
        } else {
            displayError("No Quote Found");
        }
    } else {
        Serial.println("Failed to fetch thought");
        displayError("Quote Fetch Failed");
    }
    http.end();
}