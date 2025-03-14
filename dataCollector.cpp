#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include "esp_sntp.h"
#include <ArduinoJson.h>
#include <ExpirationTimer.h>
#include "credentials.h"
#include "DataCollector.h"
#include "data.h"

#define PROPANE_TIMEOUT (3600 * 6)
#define TEMPERATURE_TIMEOUT 300
#define SETTIME_TIMEOUT (3600 * 24)
// #define SETTIME_TIMEOUT 60
#define TIMEZONE "America/Los_Angeles"

extern volatile struct data_struct data;

String token;
long tokenExpires = 0;
bool callApi(const char* endpoint, const char* requestType, void** doc) {
  char url[255];
  bool doPost = false;
  char postData[128];

  WiFiClientSecure client;
  HTTPClient https;
  client.setInsecure();

  strncpy(url, endpoint, sizeof(url));
  if (strcmp(requestType, "propane") == 0) {
    String authorization = "Basic " + String(PROPANE_CREDENTIALS_BASE64);
    https.addHeader("Authorization", authorization);
  } else if (strcmp(requestType, "temperature") == 0) {
    strncat(url, OPEN_WEATHER_MAP_API_KEY, sizeof(url));
  } else if (memcmp(requestType, "cgm", 3) == 0) {
    bool performLogin = (memcmp(&requestType[3], "Login", 5) == 0);

    // Serial.println("[HTTPS] begin...");
    https.addHeader("Content-Type", "application/json");
    https.addHeader("product", "llu.android");
    https.addHeader("version", "4.9.0");
    https.addHeader("user-agent", "curl/8.4.0");
    https.addHeader("accept", "*/*");
    if (performLogin) {
      sprintf(postData, "{\"email\":\"%s\",\"password\":\"%s\"}", CGM_USERNAME, CGM_PASSWORD);
      doPost = true;
    } else {
      String authorization = "Bearer " + token;
      https.addHeader("Authorization", authorization);
    }
  }

  if (!https.begin(client, url)) {  // HTTPS
    Serial.println("[HTTPS] Unable to connect");
    https.end();
    return false;
  }

  int httpCode;
  if (doPost) {
    // Serial.println("[HTTPS] POST...");
    httpCode = https.POST(postData);
  } else {
    // Serial.println("[HTTPS] GET...");
    httpCode = https.GET();
  }

  // httpCode will be negative on error
  if (httpCode <= 0) {
    Serial.printf("[HTTPS] request failed, error: %s\n", https.errorToString(httpCode).c_str());
    https.end();
    return false;
  }

  if ((httpCode != HTTP_CODE_OK) &&
      (httpCode != HTTP_CODE_MOVED_PERMANENTLY)) {
    Serial.printf("[HTTPS] response code: %d\n", httpCode);
    Serial.println("[HTTPS] request not processed due to response code");
    https.end();
    return false;
  }

  String payload = https.getString();
  // Serial.println(payload);  // Print the response body
  if (strcmp(requestType, "timezoneInfo") == 0) {
    int length = payload.length() + 1;
    *doc = malloc(length);
    strcpy((char*) *doc, payload.c_str());
  } else {
    deserializeJson(*((JsonDocument*) doc), payload);
  }

  https.end();

  return true;
}

void printLocalTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("No time available (yet)");
    return;
  }
  Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");
}

void timeSyncCallback(struct timeval *tv) {
  data.forceDisplayTimeUpdate = true;
  data.forceLoRaTimeUpdate = true;
  Serial.println("Got time adjustment from NTP!");
  printLocalTime();
}

long httpsTaskHighWaterMark = LONG_MAX;
ExpirationTimer propaneExpirationTimer = ExpirationTimer();
ExpirationTimer temperatureExpirationTimer = ExpirationTimer();
ExpirationTimer settimeTimer = ExpirationTimer();
void vHttpsTask(void* pvParameters) {
  propaneExpirationTimer.forceExpired();
  temperatureExpirationTimer.forceExpired();
  settimeTimer.forceExpired();

  sntp_setoperatingmode(SNTP_OPMODE_POLL);
  sntp_setservername(0, "pool.ntp.org");
  sntp_set_time_sync_notification_cb(timeSyncCallback);
  sntp_init();

  while (true) {
    try {
      JsonDocument doc;

      if (settimeTimer.isExpired(SETTIME_TIMEOUT * 1000)) {
        char* payload;
        if (callApi("https://buiten.com/timezone-info", "timezoneInfo", (void**) &payload)) {
          char* originalPayload = payload;

          const int MAX_TOKENS = 5;
          char* payloadSavePtr;
          char* line = strtok_r(payload, "\n", &payloadSavePtr);
          while (line) {
            // Serial.println(line);
            if (*line == '#') {
              line = strtok_r(NULL, "\n", &payloadSavePtr);
              // Serial.println("Skipping timezone info line because it's a comment");
              continue;
            }

            char* tokens[MAX_TOKENS];
            int tokenCount = 0;
            char* tokenSavePtr;
            char* token = strtok_r(line, ",", &tokenSavePtr);
            while (token &&
                   (tokenCount < MAX_TOKENS)) {
              tokens[tokenCount++] = token;
              token = strtok_r(NULL, ",", &tokenSavePtr);
            }

            // Serial.print("tokenCount = ");
            // Serial.println(tokenCount);

            if (tokenCount < 2) {
              Serial.println("Skipping timezone info line because there are not enough tokens");
              line = strtok_r(NULL, "\n", &payloadSavePtr);
              continue;
            }

            if (strcmp(tokens[1], TIMEZONE) != 0) {
              // Serial.println("Skipping timezone info line because there is a timezone mismatch");
              line = strtok_r(NULL, "\n", &payloadSavePtr);
              continue;
            }

            if ((tokenCount == 3) &&
                (strcmp(tokens[0], "1") == 0)) {
              try {
                data.standardTimezoneOffset = atoi(tokens[2]);
              } catch (...) {
                Serial.println("Error parsing timezone info tokens");
              }
            } else if ((tokenCount == 5) &&
                       (strcmp(tokens[0], "2") == 0)) {
              try {
                data.dstBegin = atoll(tokens[2]);
                data.dstEnd = atoll(tokens[3]);
                data.daylightTimezoneOffset = atoi(tokens[4]);
                // Serial.println("---");
                // Serial.println(tokens[2]);
                // Serial.println(data.dstBegin);
                // Serial.println(tokens[3]);
                // Serial.println(data.dstEnd);
                // Serial.println(data.daylightTimezoneOffset);
                // Serial.println(time(nullptr));
              } catch (...) {
                Serial.println("Error parsing timezone info tokens");
              }
            } else {
              Serial.print("Unknown timezone info type ");
              Serial.print(tokens[0]);
              Serial.print(" with token count ");
              Serial.println(tokenCount);
            }

            line = strtok_r(NULL, "\n", &payloadSavePtr);
          }

          data.forceDisplayTimeUpdate = true;
          data.forceLoRaTimeUpdate = true;
          free((void*) originalPayload);
        }

        settimeTimer.reset();
      }

      if (time(nullptr) > tokenExpires) {
        if (callApi("https://api.libreview.io/llu/auth/login", "cgmLogin", (void**) &doc)) {
          token = (const char*) doc["data"]["authTicket"]["token"];
          tokenExpires = (long) doc["data"]["authTicket"]["expires"];

          struct tm timeinfo;
          gmtime_r((const time_t*) &tokenExpires, &timeinfo);
          Serial.print("Auth token expires at ");
          Serial.println(asctime(&timeinfo));
        }
      }

      if (time(nullptr) < tokenExpires) {
        if (callApi("https://api.libreview.io/llu/connections", "cgmNologin", (void**) &doc)) {
          JsonObject connection = doc["data"][0];
          data.mgPerDl = (long) connection["glucoseMeasurement"]["ValueInMgPerDl"];
          const char* timestamp = (const char*) connection["glucoseMeasurement"]["Timestamp"];
          Serial.print("Glucose level = ");
          Serial.print(data.mgPerDl);
          Serial.print(" mg/dL at ");
          Serial.println(timestamp);
        }
      } else {
        Serial.println("Auth token is outdated!");
        Serial.println("It should be automatically reauthorized the next time we attempt to fetch the data.");
        Serial.println("If not then the login credentials are bad");
      }

      if (propaneExpirationTimer.isExpired(PROPANE_TIMEOUT * 1000)) {
        if (callApi("https://ws.otodatanetwork.com/neevoapp/v1/DataService.svc/GetAllDisplayPropaneDevices", "propane", (void**) &doc)) {
          data.propaneLevel = (int) doc[0]["Level"];
          Serial.print("Propane level = ");
          Serial.print(data.propaneLevel);
          Serial.println("%");
        }

        propaneExpirationTimer.reset();
      }

      if (temperatureExpirationTimer.isExpired(TEMPERATURE_TIMEOUT * 1000)) {
        if (callApi("https://api.openweathermap.org/data/2.5/weather?lat=47.3874978&lon=-122.1391124&appid=", "temperature", (void**) &doc)) {
          data.temperature = (((double) doc["main"]["temp"] - 273.15) * (9/5)) + 32;
          Serial.print("Temperature = ");
          Serial.println(data.temperature);
        }

        temperatureExpirationTimer.reset();
      }

      if (uxTaskGetStackHighWaterMark(NULL) < httpsTaskHighWaterMark) {
        Serial.printf("httpsTaskHighWaterMark has changed from %d to %d\n", (httpsTaskHighWaterMark == LONG_MAX ? -1 : httpsTaskHighWaterMark), uxTaskGetStackHighWaterMark(NULL));
        httpsTaskHighWaterMark = uxTaskGetStackHighWaterMark(NULL);
      }
    } catch (...) {
    }

    vTaskDelay(60000);
  }
}
