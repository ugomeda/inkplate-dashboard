#include "Inkplate.h"
#include "config.h"
#include "esp_wifi.h"

#define DISPLAY_WIDTH 825
#define DISPLAY_HEIGHT 1200
#define STATUS_BAR_HEIGHT 60
#define MAX_ETAG_LENGTH 255

#if !defined(ARDUINO_INKPLATE10) && !defined(ARDUINO_INKPLATE10V2)
#error "Wrong board selection for this example, please select e-radionica Inkplate10 or Soldered Inkplate10 in the boards menu."
#endif

#define uS_TO_S_FACTOR 1000000 // Conversion factor for micro seconds to seconds

Inkplate display(INKPLATE_3BIT);
HTTPClient http;

// The etag of the current image displayed
RTC_DATA_ATTR char etag[MAX_ETAG_LENGTH + 1] = {0};

// Controls if the status bar messages are displayed or not
bool silent = false;

// After deep sleep, the first status displayed needs a full refresh
bool status_displayed = false;

// will be overriden with the max-age provided by the server
uint16_t sleep_duration = DEFAULT_REFRESH_TIME_SECONDS;

void display_centered_text(const char *text, uint16_t x, uint16_t y, uint16_t w,
                           uint16_t h) {
  int16_t x1, y1;
  uint16_t w1, h1;
  display.getTextBounds(text, x, y, &x1, &y1, &w1, &h1);
  display.setCursor(x + (w - w1) / 2, y + (h - h1) / 2);
  display.print(text);
}

void show_startup_message() {
  display.selectDisplayMode(INKPLATE_1BIT);
  display.setTextColor(BLACK, WHITE);
  display.setTextSize(10);
  display_centered_text("Hello!", 0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);
  display.display();

  status_displayed = true;
}

void _load_status(const char *message, int fg_color, int bg_color) {
  int16_t top = DISPLAY_HEIGHT - STATUS_BAR_HEIGHT;
  display.fillRect(0, top, DISPLAY_WIDTH, STATUS_BAR_HEIGHT, bg_color);
  display.drawThickLine(0, top, DISPLAY_WIDTH, top, fg_color, 2);
  display.setTextWrap(false);
  display.setTextColor(fg_color);
  display.setCursor(15, top + 18);
  display.setTextSize(3);
  display.print(message);
}

void display_status(const char *message, bool ignore_silent = false) {
  Serial.printf("[STATUS] %s\n", message);

  if (silent && !ignore_silent) {
    return;
  }

  display.selectDisplayMode(INKPLATE_1BIT);

  // When waking up from deep sleep, the background is not switched
  // back to white. This forces a refresh by pre-loading the exact
  // opposite of what we will display
  if (!status_displayed) {
    _load_status(message, WHITE, BLACK);
    display.preloadScreen();
    display.clearDisplay();
  }

  _load_status(message, BLACK, WHITE);
  display.partialUpdate(INKPLATE_FORCE_PARTIAL);

  // Reset the etag to force a full refresh
  etag[0] = '\0';
  status_displayed = true;
}

void load_and_display_image() {
  // FIXME Check content type
  // FIXME Display errors if needed
  // FIXME BUFFER OVERFLOW ETAG
  display_status("Requesting image...");
  Serial.println("Fetching new image...");

  float voltage = display.readBattery();
  char voltageStr[10];
  sprintf(voltageStr, "%.2fV", voltage);

  // Prepare the request
  http.begin(SERVER_URL);
  http.addHeader("X-Inkplate-Battery-Voltage", voltageStr);
  http.setUserAgent("Inkplate ePaper Client");
  if (strlen(etag)) {
    http.addHeader("If-None-Match", etag);
  }
  const char *headerkeys[] = {"content-type", "etag", "x-inkplate-next-refresh"};
  http.collectHeaders(headerkeys, 3);

  // Launch request and check result
  int httpCode = http.GET();
  if (httpCode == HTTP_CODE_NOT_MODIFIED) {
    Serial.println("[HTTP] Server returned 304, nothing to update");

    // handle the delay
    if (http.hasHeader("x-inkplate-next-refresh")) {
      String next_refresh_header = http.header("x-inkplate-next-refresh");
      int next_refresh = atoi(next_refresh_header.c_str());
      if (next_refresh > 0 && next_refresh <= UINT16_MAX) {
        sleep_duration = next_refresh;
      }
    }
  } else if (httpCode == HTTP_CODE_OK) {
    display_status("Updating display...");
    Serial.println("[HTTP] Request succeeded");
    int len = http.getSize();

    display.selectDisplayMode(INKPLATE_3BIT);
    bool result = display.drawPngFromWeb(http.getStreamPtr(), 0, 0,
                                         http.getSize(), false, false);
    if (result) {
      display.display();

      // Handle the etag
      if (http.hasHeader("etag")) {
        String new_etag = http.header("etag");
        strncpy(etag, new_etag.c_str(), MAX_ETAG_LENGTH);
      }

      // handle the delay
      if (http.hasHeader("x-inkplate-next-refresh")) {
        String next_refresh_header = http.header("x-inkplate-next-refresh");
        int next_refresh = atoi(next_refresh_header.c_str());
        if (next_refresh > 0 && next_refresh <= UINT16_MAX) {
          sleep_duration = next_refresh;
        }
      }
    } else {
      display_status("Error: unable to render image", true);
    }
  } else if (httpCode > 0) {
    display_status("Error: unexpected response from server", true);
  } else {
    display_status("Error: unable to contact server", true);
  }
  Serial.println("[HTTP] Request end");
  http.end();
}

uint8_t check_wifi_connection(uint8_t max_attempts = 10) {
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  uint8_t attempts = 0;
  while (WiFi.status() != WL_CONNECTED) {
    if (attempts > max_attempts) {
      return 1;
    }

    if (attempts % 3 == 0) {
      display_status("Connecting to Wifi.");
    }
    if (attempts % 3 == 1) {
      display_status("Connecting to Wifi..");
    }
    if (attempts % 3 == 2) {
      display_status("Connecting to Wifi...");
    }

    attempts++;
    delay(1000);
  }

  return 0;
}

void go_back_to_sleep(uint16_t sleep_time) {
  WiFi.disconnect();
  esp_sleep_enable_timer_wakeup(sleep_time * uS_TO_S_FACTOR);
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_36, LOW);
  return esp_deep_sleep_start();
}

void setup() {
  // Initialize serial and display
  Serial.begin(115200);
  display.begin();
  display.setRotation(3);

  // Check the reason of the boot
  esp_sleep_wakeup_cause_t wakeup_reason;
  wakeup_reason = esp_sleep_get_wakeup_cause();
  if (wakeup_reason == ESP_SLEEP_WAKEUP_TIMER) {
    // We're doing a normal refresh
    silent = true;
  } else if (wakeup_reason == ESP_SLEEP_WAKEUP_EXT0) {
    // The user pushed the WAKE button, show immediate feedback
  } else {
    // Initial startup
    show_startup_message();
  }

  // Connect to wifi
  if (check_wifi_connection() != 0) {
    // Go back to sleep if failed
    display_status("Failed to connect to Wifi", true);
    return go_back_to_sleep(sleep_duration);
  }

  // Display the image
  load_and_display_image();

  // Go back to sleep
  return go_back_to_sleep(sleep_duration);
}

void loop() {
}