#include "credentials.h"
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESPAsyncWebServer.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <SoftwareSerial.h>
#include "checksum.h"

// 설정
SoftwareSerial rs485(D2, D1); // RX, TX
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
WiFiClient espClient;
PubSubClient mqtt(espClient);

const char *configPath = "/config.json";
String mqtt_server = "192.168.1.100"; // 초기값

// WiFi 설정 로드 함수
void loadConfig()
{
  if (!LittleFS.exists(configPath))
    return;
  File f = LittleFS.open(configPath, "r");
  StaticJsonDocument<256> doc;
  deserializeJson(doc, f);
  WiFi.begin(doc["ssid"] | "", doc["pass"] | "");
  f.close();
}

// 시리얼 데이터를 Hex 스트링으로 변환
String toHex(uint8_t *buf, size_t len)
{
  String res = "";
  for (size_t i = 0; i < len; i++)
  {
    if (buf[i] < 0x10)
      res += "0";
    res += String(buf[i], HEX);
  }
  return res;
}

void onWsEvent(AsyncWebSocket *s, AsyncWebSocketClient *c, AwsEventType t, void *arg, uint8_t *data, size_t len)
{
  if (t == WS_EVT_DATA)
  {
    // 웹 UI에서 수신한 Hex 명령을 RS485로 전송
    rs485.write(data, len);
  }
}

void setup()
{
  Serial.begin(115200);
  rs485.begin(9600);
  LittleFS.begin();

  loadConfig();

  // AP 모드 예외 처리: 15초 내 연결 실패 시 AP 모드
  unsigned long startTask = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startTask < 15000)
  {
    delay(500);
  }
  if (WiFi.status() != WL_CONNECTED)
  {
    WiFi.softAP("Wallpad_Setup", "12345678");
  }

  // WebServer 루트
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *r)
            { r->send(LittleFS, "/index.html", "text/html"); });

  // WiFi 저장 엔드포인트
  server.on("/connect2ssid", HTTP_POST, [](AsyncWebServerRequest *r)
            {
        String ssid = r->arg("ssid");
        String pass = r->arg("pass");
        File f = LittleFS.open(configPath, "w");
        StaticJsonDocument<256> doc;
        doc["ssid"] = ssid; doc["pass"] = pass;
        serializeJson(doc, f);
        f.close();
        r->send(200, "text/plain", "Saved. Rebooting...");
        delay(2000); ESP.restart(); });

  ws.onEvent(onWsEvent);
  server.addHandler(&ws);
  server.begin();

  mqtt.setServer(mqtt_server.c_str(), 1883);
}

void loop()
{
  if (WiFi.status() == WL_CONNECTED && !mqtt.connected())
  {
    if (mqtt.connect("WallpadBridge"))
    {
      mqtt.subscribe("home/wallpad/set");
    }
  }
  mqtt.loop();

  // RS485 -> MQTT & Web
  if (rs485.available())
  {
    uint8_t buf[32];
    size_t len = rs485.readBytes(buf, 32);
    String hex = toHex(buf, len);

    mqtt.publish("home/wallpad/state", hex.c_str());
    ws.textAll("RX: " + hex);
  }
}