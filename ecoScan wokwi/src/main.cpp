#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include <DHT.h>
#include "secrets.h"
#include "firebase_root_ca.h"

#define USE_INSECURE_TLS 1

constexpr int DHT_PIN = 15;
constexpr int LDR_PIN = 34;
constexpr int MQ2_PIN = 35;
constexpr int LED_PIN = 2;
constexpr int MAX_PENDING = 20;
constexpr unsigned long SENSOR_INTERVAL = 3000;
constexpr unsigned long COMMAND_INTERVAL = 5000;
constexpr unsigned long RECONNECT_INTERVAL = 5000;
constexpr unsigned long AUTH_RETRY_INTERVAL = 30000;

DHT dht(DHT_PIN, DHT22);

struct Reading {
  float temperature;
  float humidity;
  int light;
  int gas;
  int comfort;
  unsigned long uptime;
  unsigned long sequence;
};

Reading queue[MAX_PENDING];
int head = 0;
int queueSize = 0;
unsigned long nextSequence = 0;
unsigned long bootId = 0;
unsigned long lastReading = 0;
unsigned long lastCommand = 0;
unsigned long lastWifiAttempt = 0;
unsigned long authExpiry = 0;
unsigned long lastAuthAttempt = 0;
bool authAttempted = false;
String token;

WiFiClientSecure secureClient;

void configureTls(WiFiClientSecure &c) {
#if USE_INSECURE_TLS
  c.setInsecure();
#else
  c.setCACert(FIREBASE_ROOT_CA);
#endif
  c.setHandshakeTimeout(60);
  c.setTimeout(15);
}

int comfortIndex(float temperature, float humidity, int light, int gas) {
  int comfort = 100;
  if (temperature < 20 || temperature > 26) comfort -= 20;
  if (humidity < 40 || humidity > 60) comfort -= 20;
  if (light < 500) comfort -= 15;
  if (gas > 1500) comfort -= 45;
  return max(0, comfort);
}

int request(const String &method, const String &url, const String &body, String &response) {
  response = "";

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Error: WiFi desconectado");
    return -1;
  }

  secureClient.stop();

  HTTPClient http;
  http.setConnectTimeout(15000);
  http.setTimeout(15000);
  http.setReuse(false);

  if (!http.begin(secureClient, url)) {
    Serial.println("Error al iniciar HTTPClient");
    return -1;
  }

  if (body.length() > 0) {
    http.addHeader("Content-Type", "application/json");
  }

  int status = http.sendRequest(method.c_str(), body);

  if (status > 0) {
    response = http.getString();
    Serial.printf("HTTP: %d\n", status);
  } else {
    Serial.printf("Error HTTPS: %s\n", http.errorToString(status).c_str());
    char detalle[160] = {0};
    int codigo = secureClient.lastError(detalle, sizeof(detalle));
    Serial.printf("TLS: %d - %s (heap libre %u)\n", codigo, detalle, ESP.getFreeHeap());
  }

  http.end();
  secureClient.stop();
  return status;
}

bool authenticate() {
  if (WiFi.status() != WL_CONNECTED) return false;
  if (time(nullptr) < 1700000000) return false;
  if (token.length() && (int32_t)(millis() - authExpiry) < 0) return true;
  if (authAttempted && millis() - lastAuthAttempt < AUTH_RETRY_INTERVAL) return false;

  authAttempted = true;
  lastAuthAttempt = millis();

  DynamicJsonDocument payload(512);
  payload["email"] = DEVICE_EMAIL;
  payload["password"] = DEVICE_PASSWORD;
  payload["returnSecureToken"] = true;

  String body;
  serializeJson(payload, body);

  String response;
  const String url = String("https://identitytoolkit.googleapis.com/v1/accounts:signInWithPassword?key=") + FIREBASE_API_KEY;
  int status = request("POST", url, body, response);

  if (status != 200) {
    token = "";
    Serial.printf("Autenticacion fallida: %d\n", status);
    return false;
  }

  DynamicJsonDocument result(4096);
  if (deserializeJson(result, response)) return false;

  token = result["idToken"].as<String>();
  unsigned long expires = result["expiresIn"].as<unsigned long>();

  if (!expires) expires = 3600;

  authExpiry = millis() + (min(expires, 3500UL) * 1000UL);

  if (token.length() > 0) authAttempted = false;

  return token.length() > 0;
}

String firebaseUrl(const String &path) {
  return String(FIREBASE_DATABASE_URL) + path + ".json?auth=" + token;
}

int databaseRequest(const String &method, const String &path, const String &body, String &response) {
  if (!authenticate()) return -1;

  int status = request(method, firebaseUrl(path), body, response);

  if (status == 401 || status == 403) token = "";

  return status;
}

void addReading(const Reading &reading) {
  if (queueSize == MAX_PENDING) {
    head = (head + 1) % MAX_PENDING;
    queueSize--;
    Serial.println("Buffer lleno: lectura mas antigua descartada");
  }

  queue[(head + queueSize) % MAX_PENDING] = reading;
  queueSize++;
}

void captureReading() {
  float humidity = dht.readHumidity();
  float temperature = dht.readTemperature();

  if (isnan(humidity) || isnan(temperature)) {
    Serial.println("Fallo de lectura DHT22");
    return;
  }

  Reading reading = {
    temperature,
    humidity,
    analogRead(LDR_PIN),
    analogRead(MQ2_PIN),
    0,
    millis(),
    ++nextSequence
  };

  reading.comfort = comfortIndex(
    reading.temperature,
    reading.humidity,
    reading.light,
    reading.gas
  );

  addReading(reading);

  Serial.printf(
    "Temp %.1f C, humedad %.1f %%, LDR %d, gas ADC %d, confort %d, pendientes %d\n",
    reading.temperature,
    reading.humidity,
    reading.light,
    reading.gas,
    reading.comfort,
    queueSize
  );
}

String readingJson(const Reading &r) {
  StaticJsonDocument<384> doc;

  doc["Temperatura"] = r.temperature;
  doc["Humedad"] = r.humidity;
  doc["Luz"] = r.light;
  doc["Gas"] = r.gas;
  doc["Confort"] = r.comfort;
  doc["dispositivo"] = "esp32-01";
  doc["secuencia"] = r.sequence;
  doc["inicio"] = bootId;
  doc["uptimeMs"] = r.uptime;
  doc["timestamp"][".sv"] = "timestamp";

  String payload;
  serializeJson(doc, payload);

  return payload;
}

void sendPending() {
  if (!queueSize || WiFi.status() != WL_CONNECTED) return;
  if (!authenticate()) return;

  int processed = 0;

  while (queueSize && processed < 3) {
    Reading &r = queue[head];
    const String id = String(bootId, HEX) + "-" + String(r.sequence);
    String response;

    int status = databaseRequest(
      "PUT",
      "/Historial/" + id,
      readingJson(r),
      response
    );

    if (status < 200 || status >= 300) break;

    status = databaseRequest(
      "PATCH",
      "/",
      readingJson(r),
      response
    );

    if (status < 200 || status >= 300) break;

    head = (head + 1) % MAX_PENDING;
    queueSize--;
    processed++;
  }
}

void receiveCommand() {
  if (WiFi.status() != WL_CONNECTED) return;

  String response;
  int status = databaseRequest("GET", "/Comandos/LED", "", response);

  if (status != 200 || response == "null") return;

  StaticJsonDocument<512> doc;
  if (deserializeJson(doc, response)) return;
  if (!doc["encendido"].is<bool>()) return;

  bool enabled = doc["encendido"].as<bool>();

  digitalWrite(LED_PIN, enabled ? HIGH : LOW);

  StaticJsonDocument<256> state;
  state["encendido"] = enabled;
  state["timestamp"][".sv"] = "timestamp";

  String payload;
  serializeJson(state, payload);

  String ignored;
  databaseRequest("PUT", "/EstadoActuador/LED", payload, ignored);
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  dht.begin();
  bootId = esp_random();

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.println("Conectando al WiFi...");

  int intentosWifi = 0;

  while (WiFi.status() != WL_CONNECTED && intentosWifi < 30) {
    delay(1000);
    intentosWifi++;
  }

  Serial.printf(
    "WiFi conectado: %s\n",
    WiFi.status() == WL_CONNECTED ? "SI" : "NO"
  );

  Serial.print("IP del ESP32: ");
  Serial.println(WiFi.localIP());

  configTime(0, 0, "pool.ntp.org", "time.google.com");

  time_t ahora = time(nullptr);
  int intentos = 0;

  while (ahora < 1700000000 && intentos < 30) {
    delay(1000);
    ahora = time(nullptr);
    intentos++;
  }

  Serial.printf("Hora ESP32: %lld\n", (long long)ahora);
  Serial.printf(
    "Hora sincronizada: %s\n",
    ahora >= 1700000000 ? "SI" : "NO"
  );

  configureTls(secureClient);

  lastWifiAttempt = millis();

  Serial.println("Estacion ambiental iniciada");
}

void loop() {
  unsigned long now = millis();

  if (WiFi.status() != WL_CONNECTED &&
      now - lastWifiAttempt >= RECONNECT_INTERVAL) {
    lastWifiAttempt = now;
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  }

  if (now - lastReading >= SENSOR_INTERVAL) {
    lastReading = now;
    captureReading();
  }

  if (WiFi.status() == WL_CONNECTED) {
    sendPending();

    if (now - lastCommand >= COMMAND_INTERVAL) {
      lastCommand = now;
      receiveCommand();
    }
  }

  delay(20);
}
