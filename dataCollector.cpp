#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <ExpirationTimer.h>
#include "credentials.h"
#include "DataCollector.h"
#include "data.h"

#define PROPANE_TIMEOUT (3600 * 6)
#define TEMPERATURE_TIMEOUT 300

extern volatile struct data_struct data;

String token;
long tokenExpires = 0;
bool callApi(const char* endpoint, const char* requestType, JsonDocument* doc) {
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
  deserializeJson(*doc, payload);

  https.end();

  return true;
}

long httpsTaskHighWaterMark = LONG_MAX;
ExpirationTimer propaneExpirationTimer = ExpirationTimer();
ExpirationTimer temperatureExpirationTimer = ExpirationTimer();
void vHttpsTask(void* pvParameters) {
  propaneExpirationTimer.forceExpired();
  temperatureExpirationTimer.forceExpired();

  while (true) {
    try {
      JsonDocument doc;

      if (time(nullptr) > tokenExpires) {
        if (callApi("https://api.libreview.io/llu/auth/login", "cgmLogin", &doc)) {
          token = (const char*) doc["data"]["authTicket"]["token"];
          tokenExpires = (long) doc["data"]["authTicket"]["expires"];

          struct tm timeinfo;
          gmtime_r((const time_t*) &tokenExpires, &timeinfo);
          Serial.print("Auth token expires at ");
          Serial.println(asctime(&timeinfo));
        }
      }

      if (time(nullptr) < tokenExpires) {
        if (callApi("https://api.libreview.io/llu/connections", "cgmNologin", &doc)) {
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
        if (callApi("https://ws.otodatanetwork.com/neevoapp/v1/DataService.svc/GetAllDisplayPropaneDevices", "propane", &doc)) {
          data.propaneLevel = (int) doc[0]["Level"];
          Serial.print("Propane level = ");
          Serial.print(data.propaneLevel);
          Serial.println("%");
        }

        propaneExpirationTimer.reset();
      }

      if (temperatureExpirationTimer.isExpired(TEMPERATURE_TIMEOUT * 1000)) {
        if (callApi("https://api.openweathermap.org/data/2.5/weather?lat=47.3874978&lon=-122.1391124&appid=", "temperature", &doc)) {
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
