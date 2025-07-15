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
const char* newsAPI = "https://api.thenewsapi.com/v1/news/top?api_token=YOUR_NEWS_API_KEY&locale=us";
const char* poemAPI = "https://poetrydb.org/random";
const char* thoughtAPI = "https://zenquotes.io/api/today";
const char* geoAPI = "http://ip-api.com/json";  // Get location based on IP

// NTP Client Setup
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 19800, 60000);

// GPIO Pin for Mode Switch
const int wake_up = 5;
const int reset = 4;
const int switchPin = 15;
volatile int displayMode = 0; // 0: Calendar+Weather, 1: News, 2: Poem, 3: Thought

// Days and Months
const char* days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
const char* months[] = {"January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"};

// Global Variables for Location and Weather Data
String city, country;
float temp = 0, feels_like = 0;
int humidity = 0, visibility = 0;

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

// Function to Ensure WiFi Connectivity
void ensureWiFiConnected() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("Reconnecting to WiFi...");
        WiFi.disconnect();
        WiFi.reconnect();
        int attempts = 0;
        while (WiFi.status() != WL_CONNECTED && attempts < 10) {
            delay(500);
            Serial.print(".");
            attempts++;
        }
        Serial.println(WiFi.status() == WL_CONNECTED ? "Reconnected!" : "Failed to reconnect.");
    }
}

// Interrupt Handler for Mode Switch
void IRAM_ATTR handleSwitch() {
    displayMode = (displayMode + 1) % 4;  // Cycle through modes
    draw_display();
}

// Function to Get Location
void get_location() {
    HTTPClient http;
    http.begin(geoAPI);
    int httpResponseCode = http.GET();
    if (httpResponseCode == 200) {
        String payload = http.getString();
        http.end();

        DynamicJsonDocument doc(1024);  // Use DynamicJsonDocument with size
        DeserializationError error = deserializeJson(doc, payload);
        if (error) {
            Serial.print("deserializeJson() failed: ");
            Serial.println(error.c_str());
            return;
        }
        city = doc["city"].as<String>();
        country = doc["countryCode"].as<String>();

        Serial.println("Detected Location: " + city + ", " + country);
    } else {
        Serial.println("Failed to get location");
        http.end();
    }
}

// Setup
void setup() {
    Serial.begin(115200);
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nConnected!");

    epd_init(wake_up, reset);
    epd_wakeup(wake_up);
    epd_set_memory(MEM_NAND);

    timeClient.begin();
    get_location();

    // Set up interrupt for mode switch
    pinMode(switchPin, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(switchPin), handleSwitch, FALLING);
}

// Main Loop
void loop() {
    ensureWiFiConnected();
    draw_display();
    delay(1800000);  // 30 minutes delay
}

// Function to Draw the Display Based on Mode
void draw_display() {
    epd_clear();
    switch (displayMode) {
        case 0: fetch_weather_and_calendar(); break;
        case 1: fetch_news(); break;
        case 2: fetch_poem(); break;
        case 3: fetch_thought(); break;
    }
    epd_udpate();
    epd_enter_stopmode();  // Put the e-paper display into low-power stop mode
}

// Function to Fetch Weather and Calendar Data
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

// Function to Fetch Weather Data
void fetch_weather() {
    String weatherURL = "http://api.openweathermap.org/data/2.5/weather?q=" + city + "," + country + "&units=metric&appid=" + String(apiKey);

    HTTPClient http;
    http.begin(weatherURL);
    int httpResponseCode = http.GET();

    if (httpResponseCode == 200) {
        String payload = http.getString();
        http.end();

        DynamicJsonDocument doc(1024);  // Use DynamicJsonDocument with size
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
        http.end();
    }
}

// Function to Draw the Display
void draw_display1(int year, int month, int day, float temp, int humidity, String city, float feels_like, int visibility) {
    epd_clear();

    // --- Top Section ---
    epd_set_color(BLACK, WHITE);
    epd_set_en_font(ASCII32);
    
    char ip_str[32];
    sprintf(ip_str, "IP: %s", WiFi.localIP().toString().c_str());
    epd_disp_string(ip_str, 570, 20);

    char battery_str[32];
    sprintf(battery_str, "Battery: %.1f%%", get_battery_percentage());
    epd_disp_string(battery_str, 50, 20);

    city.toUpperCase();
    epd_disp_string(city.c_str(), 320, 20);

    // --- Calendar Section ---
    draw_calendar(year, month, day);

    // --- Weather Section ---
    epd_set_color(WHITE, DARK_GRAY);  // Heading
    epd_set_en_font(ASCII48);
    epd_disp_string("Today's Weather", 250, 450);

    epd_set_en_font(ASCII32);
    
    epd_set_color(BLACK, GRAY);  // Subheadings
    epd_disp_string("Temp:", 50, 520);
    epd_disp_string("Humidity:", 50, 570);
    epd_disp_string("Feels Like:", 450, 520);
    epd_disp_string("Visibility:", 450, 570);

    epd_set_color(BLACK, WHITE);  // Main content
    char temp_str[32], humidity_str[32], feels_like_str[32], visibility_str[32];

    sprintf(temp_str, "%.1f C", temp);
    sprintf(humidity_str, "%d%%", humidity);
    sprintf(feels_like_str, "%.1f C", feels_like);
    sprintf(visibility_str, "%dm", visibility);

    epd_disp_string(temp_str, 150, 520);
    epd_disp_string(humidity_str, 180, 570);
    epd_disp_string(feels_like_str, 580, 520);
    epd_disp_string(visibility_str, 580, 570);

    epd_udpate();  // Update the display
}

// Function to Draw the Calendar
void draw_calendar(int year, int month, int day) {
    epd_set_en_font(ASCII48);
    
    epd_set_color(WHITE, DARK_GRAY);  // Calendar heading
    char date_header[64];
    sprintf(date_header, "%s %d", months[month], year);
    epd_disp_string(date_header, 250, 80);

    epd_set_en_font(ASCII32);
    
    epd_set_color(BLACK, GRAY);  // Weekday row
    for (int i = 0; i < 7; i++) {
        epd_disp_string(days[i], 50 + i * 100, 140);
    }

    int first_day = get_first_day_of_month(year, month);
    int total_days = (month == 1) ? ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0) ? 29 : 28)
                                  : (month == 3 || month == 5 || month == 8 || month == 10) ? 30 : 31;

    int x = 50 + first_day * 100;
    int y = 180;
    for (int d = 1; d <= total_days; d++) {
        char date_str[4];
        sprintf(date_str, "%d", d);

        if (d == day) {
            epd_set_color(WHITE, BLACK);  // Highlight current date
        } else {
            epd_set_color(BLACK, WHITE);  // Normal dates
        }
        epd_disp_string(date_str, x, y);

        x += 100;
        if (x >= 750) {
            x = 50;
            y += 50;
        }
    }
}

// Function to Get First Day of the Month
int get_first_day_of_month(int year, int month) {
    struct tm timeinfo = {0};
    timeinfo.tm_year = year - 1900;
    timeinfo.tm_mon = month;
    timeinfo.tm_mday = 1;
    time_t first_day = mktime(&timeinfo);
    return localtime(&first_day)->tm_wday;
}

// Function to Get Battery Percentage
float get_battery_percentage() {
    float voltage = analogRead(35) / 4096.0 * 7.46;
    if (voltage < 3.50) return 0;
    if (voltage > 4.20) return 100;

    return constrain(2836.9625 * pow(voltage, 4) - 
                    43987.4889 * pow(voltage, 3) + 
                    255233.8134 * pow(voltage, 2) - 
                    656689.7123 * voltage + 
                    632041.7303, 0, 100);
}

// Function to Fetch News
void fetch_news() {
    HTTPClient http;
    http.begin(newsAPI);
    int httpResponseCode = http.GET();
    if (httpResponseCode == 200) {
        String payload = http.getString();
        DynamicJsonDocument doc(8192);  // Use DynamicJsonDocument with size
        DeserializationError error = deserializeJson(doc, payload);
        if (error) {
            Serial.print("deserializeJson() failed: ");
            Serial.println(error.c_str());
            return;
        }
        JsonArray articles = doc["data"].as<JsonArray>();
        if (!articles.isNull() && articles.size() > 0) {
            String headline = articles[0]["title"].as<String>();
            epd_disp_string(headline.c_str(), 100, 100);
        }
    } else {
        Serial.println("Failed to fetch news");
    }
    http.end();
}

// Function to Fetch Poem
void fetch_poem() {
    HTTPClient http;
    http.begin(poemAPI);
    int httpResponseCode = http.GET();
    if (httpResponseCode == 200) {
        String payload = http.getString();
        DynamicJsonDocument doc(1024);  // Use DynamicJsonDocument with size
        DeserializationError error = deserializeJson(doc, payload);
        if (error) {
            Serial.print("deserializeJson() failed: ");
            Serial.println(error.c_str());
            return;
        }
        if (!doc.isNull()) {
            String poem = doc[0]["lines"][0].as<String>();
            epd_disp_string(poem.c_str(), 100, 100);
        }
    } else {
        Serial.println("Failed to fetch poem");
    }
    http.end();
}

// Function to Fetch Thought
void fetch_thought() {
    HTTPClient http;
    http.begin(thoughtAPI);
    int httpResponseCode = http.GET();
    if (httpResponseCode == 200) {
        String payload = http.getString();
        DynamicJsonDocument doc(1024);  // Use DynamicJsonDocument with size
        DeserializationError error = deserializeJson(doc, payload);
        if (error) {
            Serial.print("deserializeJson() failed: ");
            Serial.println(error.c_str());
            return;
        }
        if (!doc.isNull()) {
            String quote = doc[0]["q"].as<String>();
            epd_disp_string(quote.c_str(), 100, 100);
        }
    } else {
        Serial.println("Failed to fetch thought");
    }
    http.end();
}