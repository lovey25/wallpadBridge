#include "credentials.h"
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ArduinoOTA.h>
#include <ESPAsyncWebServer.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <Ticker.h>
#include <SoftwareSerial.h>
#include "checksum.h"
#include "rs485_parser.h"
#include "device_decoder.h"
#include "device_manager.h"
#include "command_builder.h"

// 설정
// RS485는 SoftwareSerial(D2=RX, D1=TX) 사용
SoftwareSerial rs485(D2, D1);
const uint8_t MAX_WS_CLIENTS = 8;
const uint32_t WS_MEMORY_GUARD_THRESHOLD = 15000;
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
WiFiClient espClient;
PubSubClient mqtt(espClient);
Ticker watchdogTicker;

const char *configPath = "/config.json";
String wifi_ssid = WIFI_SSID;     // WiFi SSID
String wifi_pass = WIFI_PASS;     // WiFi Password
String mqtt_server = MQTT_SERVER; // MQTT 서버 (credentials.h에서 로드)
String mqtt_user = MQTT_USER;     // MQTT 사용자 (credentials.h에서 로드)
String mqtt_pass = MQTT_PASS;     // MQTT 비밀번호 (credentials.h에서 로드)
int mqtt_port = MQTT_PORT;        // MQTT 포트 (credentials.h에서 로드)

// RS485 프로토콜 엔진
RS485Parser parser;
DeviceManager deviceManager;

// Binary 센서 핀
const int BINARY_PIN_1 = D5;
const int BINARY_PIN_2 = D6;
const int BINARY_PIN_3 = D7;
bool binarySensor1 = false;
bool binarySensor2 = false;
bool binarySensor3 = false;

// WiFi 스캔 결과 캐시
String cachedScanResults = "[]";
bool scanInProgress = false;
// 모니터 페이지 활성 상태 추적 (웹소켓 연결 우선 제어)
const bool ENABLE_MONITOR_PRIORITY = false;
bool monitorSessionActive = false;
unsigned long lastMonitorActivityMs = 0;
const unsigned long MONITOR_ACTIVE_TTL_MS = 15000;
// 파일 시스템 접근 보호
bool fileSystemBusy = false;
unsigned long lastFileAccess = 0;
const unsigned long FILE_ACCESS_DELAY = 50; // 50ms 최소 간격
// 메모리 모니터링
unsigned long lastMemoryCheck = 0;
const unsigned long MEMORY_CHECK_INTERVAL = 10000; // 10초
const uint32_t MEMORY_WARNING_THRESHOLD = 10000;   // 10KB 이하 시 경고 (상향)

// RS485 송신 최소 간격 제어 (프로토콜 문서: TX Interval 50ms)
unsigned long lastRS485TxTime = 0;
const unsigned long RS485_TX_MIN_INTERVAL_MS = 50;

// WebSocket → RS485 대기 버퍼
// (async 콜백 컨텍스트에서 SoftwareSerial 직접 호출 시 비트뱅잉 타이밍 오류 발생)
// → loop() 컨텍스트에서 안전하게 처리
uint8_t wsCommandBuffer[32];
size_t wsCommandLen = 0;
bool wsCommandPending = false;

// RS485 활동 감시 Watchdog
unsigned long lastRS485ActivityTime = 0;
bool rs485ActivityWatchdogEnabled = false;
const unsigned long RS485_WATCHDOG_ENABLE_DELAY_MS = 300000; // 부팅 5분 후 활성화
const unsigned long RS485_ACTIVITY_TIMEOUT_MS = 600000;      // 10분 무수신 시 재부팅

// 연속 invalid frame 카운터 (파서 상태 꼬임 감지)
int consecutiveInvalidFrames = 0;
uint32_t rs485BytesSinceLastFrame = 0;
const int MAX_CONSECUTIVE_INVALID = 100;
const uint32_t MAX_BYTES_WITHOUT_FRAME = 512;

// Watchdog 플래그 (콜백에서 Serial.print 방지)
bool shouldCheckWifiConnection = false;
bool shouldCheckMqttConnection = false;

// WiFi 모드 추적 (AP 모드인지 Station 모드인지)
bool isAPMode = false;

// WiFi 재연결 시도 카운터
int wifiReconnectAttempts = 0;
const int MAX_WIFI_RECONNECT_ATTEMPTS = 3; // 3회 실패 시 리부팅

// 재부팅 플래그 (HTTP 응답 완료 후 안전하게 재부팅)
bool shouldReboot = false;
unsigned long rebootScheduledTime = 0;

// OTA 상태
bool otaInProgress = false;

#ifndef OTA_HOSTNAME
#define OTA_HOSTNAME "wallpad-bridge"
#endif

#ifndef OTA_PASSWORD
#define OTA_PASSWORD ""
#endif

bool isMonitorSessionActive()
{
  if (!ENABLE_MONITOR_PRIORITY)
    return false;

  if (!monitorSessionActive)
    return false;

  // millis overflow-safe 비교
  unsigned long elapsed = millis() - lastMonitorActivityMs;
  if (elapsed > MONITOR_ACTIVE_TTL_MS)
  {
    monitorSessionActive = false;
    return false;
  }

  return true;
}

void markMonitorSessionActive()
{
  if (!ENABLE_MONITOR_PRIORITY)
    return;

  monitorSessionActive = true;
  lastMonitorActivityMs = millis();
}

void markMonitorSessionInactive()
{
  if (!ENABLE_MONITOR_PRIORITY)
    return;

  monitorSessionActive = false;
}

void setupOTA()
{
  ArduinoOTA.setHostname(OTA_HOSTNAME);

  if (strlen(OTA_PASSWORD) > 0)
  {
    ArduinoOTA.setPassword(OTA_PASSWORD);
  }

  ArduinoOTA.onStart([]()
                     {
                       otaInProgress = true;
                       shouldCheckWifiConnection = false;
                       shouldCheckMqttConnection = false;
                       if (mqtt.connected())
                       {
                         mqtt.publish("home/wallpad/status", "updating", true);
                       } });

  ArduinoOTA.onEnd([]()
                   {
                     otaInProgress = false;
                     if (mqtt.connected())
                     {
                       mqtt.publish("home/wallpad/status", "online", true);
                     } });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total)
                        {
                          if (total == 0)
                            return;

                          static unsigned long lastProgressPublish = 0;
                          if (millis() - lastProgressPublish > 2000 && mqtt.connected())
                          {
                            lastProgressPublish = millis();
                            char msg[48];
                            unsigned int percent = (progress * 100U) / total;
                            snprintf(msg, sizeof(msg), "OTA progress: %u%%", percent);
                            mqtt.publish("home/wallpad/log/info", msg);
                          } });

  ArduinoOTA.onError([](ota_error_t error)
                     {
                       otaInProgress = false;
                       if (mqtt.connected())
                       {
                         char msg[64];
                         snprintf(msg, sizeof(msg), "OTA error: %u", error);
                         mqtt.publish("home/wallpad/log/warning", msg);
                         mqtt.publish("home/wallpad/status", "online", true);
                       } });

  ArduinoOTA.begin();
}

// WiFi 설정 로드 함수
void loadConfig()
{
  if (!LittleFS.exists(configPath))
  {
    // Serial.println("[CONFIG] Using defaults");
    return;
  }

  File f = LittleFS.open(configPath, "r");
  if (!f)
  {
    // Serial.println("[CONFIG] ERROR: Failed to open config.json");
    return;
  }

  StaticJsonDocument<512> doc;
  DeserializationError error = deserializeJson(doc, f);
  f.close();

  if (error)
  {
    // Serial.println("[CONFIG] ERROR: JSON parsing failed");
    return;
  }

  // WiFi 설정 로드
  wifi_ssid = doc["ssid"] | "";
  wifi_pass = doc["pass"] | "";

  // MQTT 설정 로드
  if (doc.containsKey("mqtt_server"))
    mqtt_server = doc["mqtt_server"].as<String>();
  if (doc.containsKey("mqtt_port"))
    mqtt_port = doc["mqtt_port"];
  if (doc.containsKey("mqtt_user"))
    mqtt_user = doc["mqtt_user"].as<String>();
  if (doc.containsKey("mqtt_pass"))
    mqtt_pass = doc["mqtt_pass"].as<String>();

  // Serial.printf("[CONFIG] Loaded: %s, MQTT: %s:%d\n", wifi_ssid.c_str(), mqtt_server.c_str(), mqtt_port);
}

// MQTT 콜백 함수 - MQTT로부터 명령 수신
void mqttCallback(char *topic, byte *payload, unsigned int length)
{

  String topicStr = String(topic);
  uint8_t cmdBuffer[32];
  size_t cmdLen = 0;

  // Fan 제어: home/wallpad/fan/set
  if (topicStr == "home/wallpad/fan/set")
  {
    String state = "";
    for (unsigned int i = 0; i < length; i++)
    {
      state += (char)payload[i];
    }
    bool turnOn = (state == "ON");
    cmdLen = CommandBuilder::buildFanPowerControl(turnOn, cmdBuffer, sizeof(cmdBuffer));
  }
  // Climate 제어: home/wallpad/climate/[1-2]/set
  else if (topicStr.startsWith("home/wallpad/climate/") && topicStr.endsWith("/set"))
  {
    // 토픽에서 방 번호 추출
    int roomNum = topicStr.substring(21, 22).toInt(); // "home/wallpad/climate/1/set"의 21번째 문자
    uint8_t roomAddr = 0x10 + roomNum;

    // JSON 파싱
    StaticJsonDocument<256> doc;
    DeserializationError error = deserializeJson(doc, payload, length);

    if (!error)
    {
      if (doc.containsKey("mode"))
      {
        const char *mode = doc["mode"];
        uint8_t modeVal = 0; // OFF
        if (strcmp(mode, "heat") == 0)
          modeVal = 1;
        else if (strcmp(mode, "off") == 0)
          modeVal = 4;

        cmdLen = CommandBuilder::buildClimateModeControl(roomAddr, modeVal, cmdBuffer, sizeof(cmdBuffer));
      }
      else if (doc.containsKey("target_temp"))
      {
        uint8_t temp = doc["target_temp"];
        cmdLen = CommandBuilder::buildClimateTempControl(roomAddr, temp, cmdBuffer, sizeof(cmdBuffer));
      }
    }
  }
  // 기존 방식: home/wallpad/set (JSON payload)
  else if (topicStr == "home/wallpad/set")
  {
    // JSON 파싱
    StaticJsonDocument<256> doc;
    DeserializationError error = deserializeJson(doc, payload, length);

    if (error)
    {
      // Serial.println("[MQTT] JSON parse failed");
      return;
    }

    const char *device = doc["device"];

    // 장치별 명령 생성
    if (strcmp(device, "light") == 0)
    {
      uint8_t address = doc["address"] | 0x11;
      const char *state = doc["state"];
      bool turnOn = (strcmp(state, "ON") == 0);
      cmdLen = CommandBuilder::buildLightControl(address, turnOn, cmdBuffer, sizeof(cmdBuffer));
    }
    else if (strcmp(device, "fan") == 0)
    {
      const char *state = doc["state"];
      if (doc.containsKey("speed"))
      {
        const char *speed = doc["speed"];
        uint8_t speedVal = 1;
        if (strcmp(speed, "MEDIUM") == 0)
          speedVal = 3;
        else if (strcmp(speed, "HIGH") == 0)
          speedVal = 7;
        cmdLen = CommandBuilder::buildFanSpeedControl(speedVal, cmdBuffer, sizeof(cmdBuffer));
      }
      else
      {
        bool turnOn = (strcmp(state, "ON") == 0);
        cmdLen = CommandBuilder::buildFanPowerControl(turnOn, cmdBuffer, sizeof(cmdBuffer));
      }
    }
    else if (strcmp(device, "doorlock") == 0)
    {
      cmdLen = CommandBuilder::buildDoorLockOpen(cmdBuffer, sizeof(cmdBuffer));
    }
    else if (strcmp(device, "climate") == 0)
    {
      uint8_t room = doc["room"] | 0x11;
      if (doc.containsKey("mode"))
      {
        const char *mode = doc["mode"];
        uint8_t modeVal = 4; // OFF
        if (strcmp(mode, "HEAT") == 0)
          modeVal = 1;
        else if (strcmp(mode, "AWAY") == 0)
          modeVal = 7;
        cmdLen = CommandBuilder::buildClimateModeControl(room, modeVal, cmdBuffer, sizeof(cmdBuffer));
      }
      else if (doc.containsKey("target_temp"))
      {
        uint8_t temp = doc["target_temp"];
        cmdLen = CommandBuilder::buildClimateTempControl(room, temp, cmdBuffer, sizeof(cmdBuffer));
      }
    }
  }

  // RS485로 명령 전송 (최소 TX 간격 보장: 프로토콜 문서 TX Interval 50ms)
  if (cmdLen > 0)
  {
    unsigned long sinceLastTx = millis() - lastRS485TxTime;
    if (sinceLastTx < RS485_TX_MIN_INTERVAL_MS)
    {
      delay(RS485_TX_MIN_INTERVAL_MS - sinceLastTx);
    }
    rs485.write(cmdBuffer, cmdLen);
    lastRS485TxTime = millis();
    String hexCmd = CommandBuilder::toHexString(cmdBuffer, cmdLen);
    // Serial.println("[MQTT->RS485] TX: " + hexCmd);
    if (ws.count() > 0 && ESP.getFreeHeap() > 12000)
    {
      ws.textAll("TX: " + hexCmd);
    }
  }
  else
  {
    // Serial.println("[MQTT] No command generated");
  }
}

// Watchdog 타이머 콜백
void watchdogCallback()
{
  // AP 모드에서는 Watchdog 체크 안 함
  if (isAPMode)
  {
    return;
  }

  // Station 모드에서만 WiFi 연결 체크
  if (!scanInProgress && WiFi.status() != WL_CONNECTED)
  {
    shouldCheckWifiConnection = true;
  }

  // MQTT 연결 확인 (WiFi 연결되었을 때만)
  if (WiFi.status() == WL_CONNECTED && !mqtt.connected())
  {
    shouldCheckMqttConnection = true;
  }
}

// WiFi 네트워크 스캔 시작 (비동기)
void performWiFiScan()
{
  if (scanInProgress)
  {
    // Serial.println("[SCAN] Already in progress, skipping");
    return;
  }

  // Serial.println("[SCAN] Starting WiFi scan...");
  scanInProgress = true;

  // 이전 스캔 결과 삭제
  WiFi.scanDelete();

  // 비동기 스캔 시작 (non-blocking, hidden SSID 제외)
  WiFi.scanNetworks(true, false);
  // Serial.println("[SCAN] Scan initiated");
}

// WiFi 스캔 완료 확인 및 결과 처리 (loop에서 호출)
void checkWiFiScanComplete()
{
  if (!scanInProgress)
    return;

  int n = WiFi.scanComplete();

  if (n == WIFI_SCAN_RUNNING)
  {
    // 아직 스캔 중
    return;
  }

  if (n >= 0)
  {
    // 스캔 완료 - 결과 처리
    // Serial.print("[SCAN] Scan complete, found ");
    // Serial.print(n);
    // Serial.println(" networks");

    int maxNetworks = (n > 20) ? 20 : n;

    String json = "[";
    for (int i = 0; i < maxNetworks; i++)
    {
      if (i > 0)
        json += ",";
      json += "{\"ssid\":\"" + WiFi.SSID(i) + "\",\"rssi\":" + String(WiFi.RSSI(i)) + "}";

      if (i % 3 == 0)
        yield(); // 3개마다 WDT 피드
    }
    json += "]";

    cachedScanResults = json;
    WiFi.scanDelete(); // 스캔 결과 메모리 해제

    // WebSocket으로 스캔 결과 브로드캐스트 (클라이언트 있고 메모리 충분할 때만)
    // Serial.print("[SCAN] WebSocket clients connected: ");
    // Serial.println(ws.count());
    // Serial.print("[SCAN] Free heap: ");
    // Serial.println(ESP.getFreeHeap());

    if (ws.count() > 0 && ESP.getFreeHeap() > 15000)
    {
      String wsMessage = "{\"type\":\"wifi_scan\",\"data\":" + json + "}";
      // Serial.println("[SCAN] Broadcasting results via WebSocket");
      ws.textAll(wsMessage);
      // Serial.println("[SCAN] Broadcast complete");
    }
    else
    {
      // Serial.println("[SCAN] Not broadcasting (no clients or low memory)");
    }
  }
  else
  {
    // 스캔 실패
    // Serial.print("[SCAN] Scan failed with code: ");
    // Serial.println(n);
    cachedScanResults = "[]";
  }

  scanInProgress = false;
}

// Binary 센서 읽기 및 MQTT 발행
void checkBinarySensors()
{
  bool sensor1 = !digitalRead(BINARY_PIN_1); // Pull-up이므로 반전
  bool sensor2 = !digitalRead(BINARY_PIN_2);
  bool sensor3 = !digitalRead(BINARY_PIN_3);

  // 상태 변경 감지 및 발행
  if (sensor1 != binarySensor1)
  {
    binarySensor1 = sensor1;
    mqtt.publish("home/wallpad/binary/1", sensor1 ? "ON" : "OFF", true);
  }
  if (sensor2 != binarySensor2)
  {
    binarySensor2 = sensor2;
    mqtt.publish("home/wallpad/binary/2", sensor2 ? "ON" : "OFF", true);
  }
  if (sensor3 != binarySensor3)
  {
    binarySensor3 = sensor3;
    mqtt.publish("home/wallpad/binary/3", sensor3 ? "ON" : "OFF", true);
  }
}

// MQTT 연결 함수
bool connectMQTT()
{
  if (!WiFi.isConnected())
    return false;

  // Serial.printf("[MQTT] Connecting to %s:%d...\n", mqtt_server.c_str(), mqtt_port);

  bool connected = false;
  if (mqtt_user.length() > 0)
  {
    connected = mqtt.connect("WallpadBridge", mqtt_user.c_str(), mqtt_pass.c_str(),
                             "home/wallpad/status", 0, true, "offline");
  }
  else
  {
    connected = mqtt.connect("WallpadBridge", "home/wallpad/status", 0, true, "offline");
  }

  if (connected)
  {
    // Serial.println("[MQTT] ✓ Connected");
    mqtt.subscribe("home/wallpad/set");
    mqtt.subscribe("home/wallpad/fan/set");
    mqtt.subscribe("home/wallpad/climate/1/set");
    mqtt.subscribe("home/wallpad/climate/2/set");
    mqtt.subscribe("home/wallpad/climate/3/set");
    mqtt.subscribe("home/wallpad/climate/4/set");
    mqtt.publish("home/wallpad/status", "online", true);
    return true;
  }
  else
  {
    // Serial.printf("[MQTT] ✗ Failed (state: %d)\n", mqtt.state());
    return false;
  }
}

// Home Assistant MQTT Discovery - 개별 엔티티 발행 (비동기)
bool publishDiscoveryEntity(int index)
{
  if (!mqtt.connected() || ESP.getFreeHeap() < 12000)
    return false;

  const char *deviceInfo = ",\"device\":{\"identifiers\":[\"wallpad_bridge\"],\"name\":\"Wallpad Bridge\",\"model\":\"ESP8266 RS485\",\"manufacturer\":\"EveryX\",\"sw_version\":\"1.0.0\"}";
  String config, topic;
  bool result = false;

  // index에 따라 엔티티 발행
  switch (index)
  {
  case 0: // Light 1 (거실조명1)
    config = "{\"name\":\"거실조명1\",\"unique_id\":\"wallpad_light_1\",\"state_topic\":\"home/wallpad/light/1/state\",\"command_topic\":\"home/wallpad/set\",\"payload_on\":\"{\\\"device\\\":\\\"light\\\",\\\"address\\\":17,\\\"state\\\":\\\"ON\\\"}\",\"payload_off\":\"{\\\"device\\\":\\\"light\\\",\\\"address\\\":17,\\\"state\\\":\\\"OFF\\\"}\",\"optimistic\":false";
    config += deviceInfo;
    config += "}";
    result = mqtt.publish("homeassistant/light/wallpad_light_1/config", config.c_str(), true);
    // Serial.println("[DISCOVERY] Published: Light 1");
    break;

  case 1: // Light 2 (거실조명2)
    config = "{\"name\":\"거실조명2\",\"unique_id\":\"wallpad_light_2\",\"state_topic\":\"home/wallpad/light/2/state\",\"command_topic\":\"home/wallpad/set\",\"payload_on\":\"{\\\"device\\\":\\\"light\\\",\\\"address\\\":18,\\\"state\\\":\\\"ON\\\"}\",\"payload_off\":\"{\\\"device\\\":\\\"light\\\",\\\"address\\\":18,\\\"state\\\":\\\"OFF\\\"}\",\"optimistic\":false";
    config += deviceInfo;
    config += "}";
    result = mqtt.publish("homeassistant/light/wallpad_light_2/config", config.c_str(), true);
    // Serial.println("[DISCOVERY] Published: Light 2");
    break;

  case 2: // Light 3 (3Way 조명)
    config = "{\"name\":\"3Way 조명\",\"unique_id\":\"wallpad_light_3\",\"state_topic\":\"home/wallpad/light/3/state\",\"command_topic\":\"home/wallpad/set\",\"payload_on\":\"{\\\"device\\\":\\\"light\\\",\\\"address\\\":19,\\\"state\\\":\\\"ON\\\"}\",\"payload_off\":\"{\\\"device\\\":\\\"light\\\",\\\"address\\\":19,\\\"state\\\":\\\"OFF\\\"}\",\"optimistic\":false";
    config += deviceInfo;
    config += "}";
    result = mqtt.publish("homeassistant/light/wallpad_light_3/config", config.c_str(), true);
    // Serial.println("[DISCOVERY] Published: Light 3");
    break;

  case 3: // Fan (환기)
    config = "{\"name\":\"환기\",\"unique_id\":\"wallpad_fan\",\"state_topic\":\"home/wallpad/fan/state\",\"command_topic\":\"home/wallpad/fan/set\",\"state_value_template\":\"{{ value_json.state }}\",\"payload_on\":\"ON\",\"payload_off\":\"OFF\"";
    config += deviceInfo;
    config += "}";
    result = mqtt.publish("homeassistant/fan/wallpad_fan/config", config.c_str(), true);
    // Serial.println("[DISCOVERY] Published: Fan");
    break;

  case 4: // Door Lock
    config = "{\"name\":\"현관 도어락\",\"unique_id\":\"wallpad_doorlock\",\"state_topic\":\"home/wallpad/doorlock/state\",\"command_topic\":\"home/wallpad/set\",\"payload_unlock\":\"{\\\"device\\\":\\\"doorlock\\\"}\",\"state_locked\":\"LOCKED\",\"state_unlocked\":\"UNLOCKED\",\"optimistic\":false";
    config += deviceInfo;
    config += "}";
    result = mqtt.publish("homeassistant/lock/wallpad_doorlock/config", config.c_str(), true);
    // Serial.println("[DISCOVERY] Published: Door Lock");
    break;

  case 5: // Climate 1 (거실 난방)
  case 6: // Climate 2 (방1 난방)
  case 7: // Climate 3 (방2 난방)
  case 8: // Climate 4 (방3 난방)
  {
    int climateNum = index - 4;
    const char *climateNames[] = {"거실 난방", "방1 난방", "방2 난방", "방3 난방"};
    uint8_t roomAddr = 0x10 + climateNum;

    config = "{\"name\":\"" + String(climateNames[climateNum - 1]) + "\",";
    config += "\"unique_id\":\"wallpad_climate_" + String(climateNum) + "\",";
    config += "\"modes\":[\"off\",\"heat\"],";
    config += "\"mode_state_topic\":\"home/wallpad/climate/" + String(climateNum) + "/state\",";
    config += "\"mode_state_template\":\"{{ value_json.mode }}\",";
    config += "\"mode_command_topic\":\"home/wallpad/climate/" + String(climateNum) + "/set\",";
    config += "\"mode_command_template\":\"{\\\"device\\\":\\\"climate\\\",\\\"room\\\":" + String(roomAddr) + ",\\\"mode\\\":\\\"{{ value }}\\\"}\",";
    config += "\"temperature_state_topic\":\"home/wallpad/climate/" + String(climateNum) + "/state\",";
    config += "\"temperature_state_template\":\"{{ value_json.target_temp }}\",";
    config += "\"temperature_command_topic\":\"home/wallpad/climate/" + String(climateNum) + "/set\",";
    config += "\"temperature_command_template\":\"{\\\"device\\\":\\\"climate\\\",\\\"room\\\":" + String(roomAddr) + ",\\\"target_temp\\\":{{ value }}}\",";
    config += "\"current_temperature_topic\":\"home/wallpad/climate/" + String(climateNum) + "/state\",";
    config += "\"current_temperature_template\":\"{{ value_json.current_temp }}\",";
    config += "\"temperature_unit\":\"C\",";
    config += "\"precision\":1.0,";
    config += "\"min_temp\":15,";
    config += "\"max_temp\":30,";
    config += "\"temp_step\":1";
    config += deviceInfo;
    config += "}";

    topic = "homeassistant/climate/wallpad_climate_" + String(climateNum) + "/config";
    result = mqtt.publish(topic.c_str(), config.c_str(), true);
    // Serial.printf("[DISCOVERY] Published: Climate %d\n", climateNum);
  }
  break;

  case 9:  // Binary Sensor 1
  case 10: // Binary Sensor 2
  case 11: // Binary Sensor 3 (마지막)
  {
    int sensorNum = index - 8;
    config = "{\"name\":\"Binary Sensor " + String(sensorNum) + "\",\"unique_id\":\"wallpad_binary_" + String(sensorNum) + "\",\"state_topic\":\"home/wallpad/binary/" + String(sensorNum) + "\",\"payload_on\":\"ON\",\"payload_off\":\"OFF\",\"device_class\":\"motion\"";
    config += deviceInfo;
    config += "}";

    topic = "homeassistant/binary_sensor/wallpad_binary_" + String(sensorNum) + "/config";
    result = mqtt.publish(topic.c_str(), config.c_str(), true);
    // Serial.printf("[DISCOVERY] Published: Binary Sensor %d\n", sensorNum);
  }
  break;

  default:
    return false;
  }

  yield(); // CPU 양보
  return result;
}

// Home Assistant MQTT Discovery 메시지 발행 (레거시 - 호환용)
void publishDiscovery()
{
  if (!mqtt.connected())
    return;

  // Serial.println("[DISCOVERY] Publishing entities (legacy mode)...");
  yield();

  // Device 정보 (모든 엔티티에 공통)
  const char *deviceInfo = ",\"device\":{\"identifiers\":[\"wallpad_bridge\"],\"name\":\"Wallpad Bridge\",\"model\":\"ESP8266 RS485\",\"manufacturer\":\"EveryX\",\"sw_version\":\"1.0.0\"}";

  int successCount = 0;

  // 1. Light 엔티티 (거실조명1, 거실조명2, 3Way 조명)
  const char *lightNames[] = {"거실조명1", "거실조명2", "3Way 조명"};
  for (int i = 1; i <= 3; i++)
  {
    if (ESP.getFreeHeap() < 12000)
      break;

    String lightId = "wallpad_light_" + String(i);
    String lightName = lightNames[i - 1];

    String config = "{";
    config += "\"name\":\"" + lightName + "\",";
    config += "\"unique_id\":\"" + lightId + "\",";
    config += "\"state_topic\":\"home/wallpad/light/" + String(i) + "/state\",";
    config += "\"command_topic\":\"home/wallpad/set\",";
    config += "\"payload_on\":\"{\\\"device\\\":\\\"light\\\",\\\"address\\\":" + String(0x10 + i) + ",\\\"state\\\":\\\"ON\\\"}\",";
    config += "\"payload_off\":\"{\\\"device\\\":\\\"light\\\",\\\"address\\\":" + String(0x10 + i) + ",\\\"state\\\":\\\"OFF\\\"}\",";
    config += "\"optimistic\":false";
    config += deviceInfo;
    config += "}";

    String topic = "homeassistant/light/" + lightId + "/config";
    if (mqtt.publish(topic.c_str(), config.c_str(), true))
      successCount++;
    yield();
    delay(100);
  }

  // 2. Fan 엔티티 (환기)
  if (ESP.getFreeHeap() > 12000)
  {
    String config = "{";
    config += "\"name\":\"환기\",";
    config += "\"unique_id\":\"wallpad_fan\",";
    config += "\"state_topic\":\"home/wallpad/fan/state\",";
    config += "\"command_topic\":\"home/wallpad/fan/set\",";
    config += "\"state_value_template\":\"{{ value_json.state }}\",";
    config += "\"payload_on\":\"ON\",";
    config += "\"payload_off\":\"OFF\"";
    config += deviceInfo;
    config += "}";

    if (mqtt.publish("homeassistant/fan/wallpad_fan/config", config.c_str(), true))
      successCount++;
    yield();
    delay(100);
  }

  // 3. Door Lock 엔티티 (현관 도어락)
  if (ESP.getFreeHeap() > 12000)
  {
    String config = "{";
    config += "\"name\":\"현관 도어락\",";
    config += "\"unique_id\":\"wallpad_doorlock\",";
    config += "\"state_topic\":\"home/wallpad/doorlock/state\",";
    config += "\"command_topic\":\"home/wallpad/set\",";
    config += "\"payload_unlock\":\"{\\\"device\\\":\\\"doorlock\\\"}\",";
    config += "\"state_locked\":\"LOCKED\",";
    config += "\"state_unlocked\":\"UNLOCKED\",";
    config += "\"optimistic\":false";
    config += deviceInfo;
    config += "}";

    if (mqtt.publish("homeassistant/lock/wallpad_doorlock/config", config.c_str(), true))
      successCount++;
    yield();
    delay(100);
  }

  // 4. Climate 엔티티 (거실 난방, 방1 난방, 방2 난방, 방3 난방)
  const char *climateNames[] = {"거실 난방", "방1 난방", "방2 난방", "방3 난방"};
  for (int i = 1; i <= 4; i++)
  {
    if (ESP.getFreeHeap() < 12000)
      break;

    String climateId = "wallpad_climate_" + String(i);
    String climateName = climateNames[i - 1];
    uint8_t roomAddr = 0x10 + i;

    String config = "{";
    config += "\"name\":\"" + climateName + "\",";
    config += "\"unique_id\":\"" + climateId + "\",";
    config += "\"modes\":[\"off\",\"heat\"],";
    config += "\"mode_state_topic\":\"home/wallpad/climate/" + String(i) + "/state\",";
    config += "\"mode_state_template\":\"{{ value_json.mode }}\",";
    config += "\"mode_command_topic\":\"home/wallpad/climate/" + String(i) + "/set\",";
    config += "\"mode_command_template\":\"{\\\"device\\\":\\\"climate\\\",\\\"room\\\":" + String(roomAddr) + ",\\\"mode\\\":\\\"{{ value }}\\\"}\",";
    config += "\"temperature_state_topic\":\"home/wallpad/climate/" + String(i) + "/state\",";
    config += "\"temperature_state_template\":\"{{ value_json.target_temp }}\",";
    config += "\"temperature_command_topic\":\"home/wallpad/climate/" + String(i) + "/set\",";
    config += "\"temperature_command_template\":\"{\\\"device\\\":\\\"climate\\\",\\\"room\\\":" + String(roomAddr) + ",\\\"target_temp\\\":{{ value }}}\",";
    config += "\"current_temperature_topic\":\"home/wallpad/climate/" + String(i) + "/state\",";
    config += "\"current_temperature_template\":\"{{ value_json.current_temp }}\",";
    config += "\"temperature_unit\":\"C\",";
    config += "\"precision\":1.0,";
    config += "\"min_temp\":15,";
    config += "\"max_temp\":30,";
    config += "\"temp_step\":1";
    config += deviceInfo;
    config += "}";

    String topic = "homeassistant/climate/" + climateId + "/config";
    if (mqtt.publish(topic.c_str(), config.c_str(), true))
      successCount++;
    yield();
    delay(100);
  }

  // 5. Binary Sensor 엔티티 (3개)
  for (int i = 1; i <= 3; i++)
  {
    if (ESP.getFreeHeap() < 12000)
      break;

    String sensorId = "wallpad_binary_" + String(i);
    String sensorName = "Binary Sensor " + String(i);

    String config = "{";
    config += "\"name\":\"" + sensorName + "\",";
    config += "\"unique_id\":\"" + sensorId + "\",";
    config += "\"state_topic\":\"home/wallpad/binary/" + String(i) + "\",";
    config += "\"payload_on\":\"ON\",";
    config += "\"payload_off\":\"OFF\",";
    config += "\"device_class\":\"motion\"";
    config += deviceInfo;
    config += "}";

    String topic = "homeassistant/binary_sensor/" + sensorId + "/config";
    if (mqtt.publish(topic.c_str(), config.c_str(), true))
      successCount++;
    yield();
    delay(100);
  }

  // Serial.printf("[DISCOVERY] ✓ Published %d entities\n", successCount);
}

void onWsEvent(AsyncWebSocket *s, AsyncWebSocketClient *c, AwsEventType t, void *arg, uint8_t *data, size_t len)
{
  if (t == WS_EVT_CONNECT)
  {
    // 연결 카운트 전에 stale 클라이언트 정리
    ws.cleanupClients();

    // Serial.printf("[WS] Client #%u connected from IP: %s\n", c->id(), c->remoteIP().toString().c_str());
    // Serial.printf("[WS] Total clients: %u\n", ws.count());

    // 메모리 부족 + 과다 접속일 때만 신규 연결 제한
    if (ws.count() > MAX_WS_CLIENTS && ESP.getFreeHeap() < WS_MEMORY_GUARD_THRESHOLD)
    {
      // Serial.printf("[WS] Max clients (3) reached, closing new connection #%u\n", c->id());
      c->close(1008, "Max connections reached");
      return;
    }
  }
  else if (t == WS_EVT_DISCONNECT)
  {
    // Serial.printf("[WS] Client #%u disconnected\n", c->id());
    // Serial.printf("[WS] Total clients: %u\n", ws.count());
  }
  else if (t == WS_EVT_ERROR)
  {
    // Serial.printf("[WS] Client #%u error: %u\n", c->id(), *((uint16_t *)arg));
  }
  else if (t == WS_EVT_PONG)
  {
    // Pong 응답 (연결 유지 확인)
    // 로그 생략 (너무 많음)
  }
  else if (t == WS_EVT_DATA)
  {
    // 데이터 크기 제한 (32바이트 이하만 허용)
    if (len > 32)
    {
      // Serial.printf("[WS] Data too large (%u bytes), ignoring\n", len);
      return;
    }
    // 웹 UI에서 수신한 데이터를 RS485 대기 버퍼에 저장
    // async 콜백 내 SoftwareSerial 직접 호출은 비트뱅잉 타이밍 오류를 유발함
    // 실제 전송은 loop() 컨텍스트에서 수행 (wsCommandPending 플래그로 처리)
    if (!wsCommandPending && len <= 32)
    {
      memcpy(wsCommandBuffer, data, len);
      wsCommandLen = len;
      wsCommandPending = true;
    }
  }
}

void setup()
{
  // RS485 SoftwareSerial 시작 (9600 baud)
  rs485.begin(9600);

  // LittleFS 초기화
  // Serial.println("\n=== Wallpad Bridge Starting ==="); // 디버그 비활성화
  if (!LittleFS.begin())
  {
    // Serial.println("[FS] Formatting...");
    LittleFS.format();
    LittleFS.begin();
  }
  // Serial.println("[FS] ✓ Ready");

  // Binary 센서 핀 초기화
  pinMode(BINARY_PIN_1, INPUT_PULLUP);
  pinMode(BINARY_PIN_2, INPUT_PULLUP);
  pinMode(BINARY_PIN_3, INPUT_PULLUP);

  loadConfig();

  // WiFi 연결 시도
  if (wifi_ssid.length() > 0)
  {
    // Serial.printf("[WIFI] Connecting to: %s\n", wifi_ssid.c_str());
    WiFi.mode(WIFI_STA);
    WiFi.begin(wifi_ssid.c_str(), wifi_pass.c_str());

    unsigned long startTask = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startTask < 30000)
    {
      delay(500);
      yield();
    }
  }
  else
  {
    // Serial.println("[WIFI] No saved credentials");
  }

  if (WiFi.status() != WL_CONNECTED)
  {
    // Serial.println("[WIFI] Failed - Starting AP mode");
    WiFi.mode(WIFI_AP);
    WiFi.softAP("Wallpad_Setup", "12345678");
    // Serial.printf("[AP] SSID: Wallpad_Setup, IP: %s\n", WiFi.softAPIP().toString().c_str());
    isAPMode = true;
  }
  else
  {
    // Serial.printf("[WIFI] ✓ Connected, IP: %s\n", WiFi.localIP().toString().c_str());
    isAPMode = false;
    WiFi.setAutoReconnect(true);
    WiFi.persistent(true);
  }

  if (!isAPMode)
  {
    setupOTA();
  }

  // WebServer 엔드포인트들
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *r)
            { 
              // 정적 파일 읽기는 fileSystemBusy 체크 불필요
              if (LittleFS.exists("/index.htm")) {
                r->send(LittleFS, "/index.htm", "text/html");
              } else {
                r->send(404, "text/plain", "File not found");
              } });

  server.on("/monitor.htm", HTTP_GET, [](AsyncWebServerRequest *r)
            { 
              // 모니터 페이지 진입 시 웹소켓 우선 모드 시작
              markMonitorSessionActive();

              // 정적 파일 읽기는 fileSystemBusy 체크 불필요
              if (LittleFS.exists("/monitor.htm")) {
                r->send(LittleFS, "/monitor.htm", "text/html");
              } else {
                r->send(404, "text/plain", "File not found");
              } });

  // 모니터 페이지 활성 ping (웹소켓 우선 모드 유지)
  server.on("/api/monitor/ping", HTTP_POST, [](AsyncWebServerRequest *r)
            {
              markMonitorSessionActive();
              r->send(204); });

  // 모니터 페이지 이탈 알림
  server.on("/api/monitor/leave", HTTP_POST, [](AsyncWebServerRequest *r)
            {
              markMonitorSessionInactive();
              r->send(204); });

  // WiFi 설정은 config.htm으로 통합
  server.on("/wifi.htm", HTTP_GET, [](AsyncWebServerRequest *r)
            { r->redirect("/config.htm"); });

  server.on("/config.htm", HTTP_GET, [](AsyncWebServerRequest *r)
            { 
              // 정적 파일 읽기는 fileSystemBusy 체크 불필요
              if (LittleFS.exists("/config.htm")) {
                r->send(LittleFS, "/config.htm", "text/html");
              } else {
                r->send(404, "text/plain", "File not found");
              } });

  server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *r)
            { 
              // 정적 파일 읽기는 fileSystemBusy 체크 불필요
              if (LittleFS.exists("/style.css")) {
                r->send(LittleFS, "/style.css", "text/css");
              } else {
                r->send(404, "text/plain", "File not found");
              } });

  // WiFi 스캔 (캐시된 결과 반환, 메모리 체크 추가)
  server.on("/scan", HTTP_GET, [](AsyncWebServerRequest *r)
            {
    // Serial.println("[SCAN] /scan endpoint called");
    
    // 메모리 부족 시 스캔 거부
    if (ESP.getFreeHeap() < 15000) {
      // Serial.println("[SCAN] Low memory, rejecting scan");
      r->send(503, "application/json", "{\"error\":\"Low memory\"}");
      return;
    }
    
    // 캐시된 결과 반환
    // Serial.print("[SCAN] Returning cached results: ");
    // Serial.println(cachedScanResults);
    r->send(200, "application/json", cachedScanResults);
    
    // 스캔이 진행 중이 아니고 파일 시스템이 바쁘지 않으면 백그라운드에서 새로운 스캔 시작
    if (!scanInProgress && !fileSystemBusy && ESP.getFreeHeap() > 20000) {
      // Serial.println("[SCAN] Triggering background scan");
      performWiFiScan();
    } else {
      // Serial.print("[SCAN] Not starting scan - scanInProgress: ");
      // Serial.print(scanInProgress);
      // Serial.print(", fileSystemBusy: ");
      // Serial.print(fileSystemBusy);
      // Serial.print(", heap: ");
      // Serial.println(ESP.getFreeHeap());
    } });

  // WiFi 상태
  server.on("/wifistatus", HTTP_GET, [](AsyncWebServerRequest *r)
            {
    String json = "{";
    json += "\"connected\":" + String(WiFi.status() == WL_CONNECTED ? "true" : "false") + ",";
    json += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
    json += "\"ssid\":\"" + WiFi.SSID() + "\",";
    json += "\"rssi\":" + String(WiFi.RSSI());
    json += "}";
    r->send(200, "application/json", json); });

  // WiFi 설정 저장 (파일 시스템 보호 추가)
  server.on("/connect2ssid", HTTP_POST, [](AsyncWebServerRequest *r)
            {
        if (fileSystemBusy || ESP.getFreeHeap() < 12000) {
          r->send(503, "text/plain", "Server busy, try again");
          return;
        }
        
        fileSystemBusy = true;
        
        String ssid = r->arg("ssid");
        String pass = r->arg("pass");
        
        // Serial.printf("[CONFIG] Save WiFi: %s\n", ssid.c_str());
        
        if (!LittleFS.begin())
        {
          fileSystemBusy = false;
          r->send(500, "text/plain", "File system error");
          return;
        }
        
        File f = LittleFS.open(configPath, "w");
        if (!f)
        {
          fileSystemBusy = false;
          r->send(500, "text/plain", "Failed to open config file");
          return;
        }
        
        f.print("{");
        f.print("\"ssid\":\""); f.print(ssid); f.print("\",");
        f.print("\"pass\":\""); f.print(pass); f.print("\",");
        f.print("\"mqtt_server\":\""); f.print(mqtt_server); f.print("\",");
        f.print("\"mqtt_port\":"); f.print(mqtt_port); f.print(",");
        f.print("\"mqtt_user\":\""); f.print(mqtt_user); f.print("\",");
        f.print("\"mqtt_pass\":\""); f.print(mqtt_pass); f.print("\"");
        f.print("}");
        f.flush();
        f.close();
        
        fileSystemBusy = false;
        // Serial.println("[CONFIG] WiFi saved, rebooting...");
        
        r->send(200, "text/plain", "Configuration saved. Rebooting...");
        
        shouldReboot = true;
        rebootScheduledTime = millis(); });

  // WiFi 리셋 (파일 시스템 보호 추가)
  server.on("/wifireset", HTTP_POST, [](AsyncWebServerRequest *r)
            {
    if (fileSystemBusy || ESP.getFreeHeap() < 12000) {
      r->send(503, "text/plain", "Server busy, try again");
      return;
    }
    
    fileSystemBusy = true;
    // Serial.println("[CONFIG] WiFi reset");
    
    if (LittleFS.exists(configPath))
    {
      LittleFS.remove(configPath);
    }
    
    fileSystemBusy = false;
    
    r->send(200, "text/plain", "Reset. Rebooting...");
    shouldReboot = true;
    rebootScheduledTime = millis(); });

  // 재부팅 (설정 유지)
  server.on("/reboot", HTTP_POST, [](AsyncWebServerRequest *r)
            {
    // Serial.println("[SYSTEM] Reboot requested");
    r->send(200, "text/plain", "Rebooting...");
    shouldReboot = true;
    rebootScheduledTime = millis(); });

  // 설정 조회 API
  server.on("/api/config", HTTP_GET, [](AsyncWebServerRequest *r)
            {
    String json = "{";
    json += "\"wifi\":{";
    json += "\"ssid\":\"" + wifi_ssid + "\",";
    json += "\"pass_length\":" + String(wifi_pass.length());
    json += "},";
    json += "\"mqtt\":{";
    json += "\"server\":\"" + mqtt_server + "\",";
    json += "\"port\":" + String(mqtt_port) + ",";
    json += "\"user\":\"" + mqtt_user + "\",";
    json += "\"pass_length\":" + String(mqtt_pass.length());
    json += "}";
    json += "}";
    
    r->send(200, "application/json", json); });

  // 설정 업데이트 API (body handler 사용, 파일 시스템 보호 추가)
  server.on(
      "/api/config", HTTP_POST,
      [](AsyncWebServerRequest *r) {},
      NULL,
      [](AsyncWebServerRequest *r, uint8_t *data, size_t len, size_t index, size_t total)
      {
        static String body;

        // 첫 번째 청크
        if (index == 0)
        {
          // 파일 시스템 사용 중이면 거부
          if (fileSystemBusy)
          {
            r->send(503, "text/plain", "Server busy, try again");
            return;
          }

          // 메모리 부족 체크
          if (ESP.getFreeHeap() < 12000)
          {
            r->send(503, "text/plain", "Low memory, try again");
            return;
          }

          body = "";
        }

        // 데이터 추가
        for (size_t i = 0; i < len; i++)
        {
          body += (char)data[i];
        }

        // 마지막 청크가 아니면 대기
        if (index + len < total)
        {
          return;
        }

        // JSON 파싱
        StaticJsonDocument<512> doc;
        DeserializationError error = deserializeJson(doc, body);

        if (error)
        {
          r->send(400, "text/plain", "Invalid JSON");
          return;
        }

        // 임시 변수에 저장
        String new_ssid = wifi_ssid;
        String new_pass = wifi_pass;
        String new_mqtt_server = mqtt_server;
        int new_mqtt_port = mqtt_port;
        String new_mqtt_user = mqtt_user;
        String new_mqtt_pass = mqtt_pass;

        // WiFi 설정 업데이트
        if (doc.containsKey("wifi"))
        {
          JsonObject wifiObj = doc["wifi"];
          if (wifiObj.containsKey("ssid"))
            new_ssid = wifiObj["ssid"].as<String>();
          if (wifiObj.containsKey("pass"))
            new_pass = wifiObj["pass"].as<String>();
        }

        // MQTT 설정 업데이트
        if (doc.containsKey("mqtt"))
        {
          JsonObject mqttObj = doc["mqtt"];
          if (mqttObj.containsKey("server"))
            new_mqtt_server = mqttObj["server"].as<String>();
          if (mqttObj.containsKey("port"))
            new_mqtt_port = mqttObj["port"];
          if (mqttObj.containsKey("user"))
            new_mqtt_user = mqttObj["user"].as<String>();
          if (mqttObj.containsKey("pass"))
            new_mqtt_pass = mqttObj["pass"].as<String>();
        }

        // 파일 시스템 확인
        if (!LittleFS.begin())
        {
          r->send(500, "text/plain", "File system error");
          return;
        }

        fileSystemBusy = true;

        // 파일 열기
        File f = LittleFS.open(configPath, "w");
        if (!f)
        {
          fileSystemBusy = false;
          r->send(500, "text/plain", "Failed to open config file");
          return;
        }

        // JSON 작성
        f.print("{");
        f.print("\"ssid\":\"");
        f.print(new_ssid);
        f.print("\",");
        f.print("\"pass\":\"");
        f.print(new_pass);
        f.print("\",");
        f.print("\"mqtt_server\":\"");
        f.print(new_mqtt_server);
        f.print("\",");
        f.print("\"mqtt_port\":");
        f.print(new_mqtt_port);
        f.print(",");
        f.print("\"mqtt_user\":\"");
        f.print(new_mqtt_user);
        f.print("\",");
        f.print("\"mqtt_pass\":\"");
        f.print(new_mqtt_pass);
        f.print("\"");
        f.print("}");
        f.flush();
        f.close();

        fileSystemBusy = false;

        // Serial.println("[CONFIG] Settings saved");

        // 응답 보내기
        r->send(200, "text/plain", "Configuration updated. Rebooting...");

        // 재부팅 예약 (2초 후 loop()에서 실행)
        shouldReboot = true;
        rebootScheduledTime = millis();
      });

  // 장치 상태 조회 API
  server.on("/api/devices", HTTP_GET, [](AsyncWebServerRequest *r)
            {
    String json = deviceManager.getAllStatesJson();
    r->send(200, "application/json", json); });

  // 프리셋 저장 (파일 시스템 보호 추가)
  server.on("/savepreset", HTTP_POST, [](AsyncWebServerRequest *r)
            {
    if (fileSystemBusy || ESP.getFreeHeap() < 12000) {
      r->send(503, "text/plain", "Server busy, try again");
      return;
    }
    
    if (r->hasArg("data")) {
      fileSystemBusy = true;
      String presetData = r->arg("data");
      File f = LittleFS.open("/preset.json", "w");
      if (f) {
        f.print(presetData);
        f.close();
        r->send(200, "text/plain", "Preset saved");
      } else {
        r->send(500, "text/plain", "Failed to save preset");
      }
      fileSystemBusy = false;
    } else {
      r->send(400, "text/plain", "Missing data");
    } });

  // 프리셋 로드 (파일 시스템 보호 추가)
  server.on("/preset.ini", HTTP_GET, [](AsyncWebServerRequest *r)
            {
    if (fileSystemBusy || millis() - lastFileAccess < FILE_ACCESS_DELAY) {
      r->send(503, "text/plain", "Server busy, try again");
      return;
    }
    
    fileSystemBusy = true;
    lastFileAccess = millis();
    
    if (LittleFS.exists("/preset.json")) {
      r->send(LittleFS, "/preset.json", "application/json");
    } else {
      r->send(404, "text/plain", "No preset found");
    }
    
    fileSystemBusy = false; });

  // WebSocket 설정
  ws.onEvent(onWsEvent);
  server.addHandler(&ws);
  server.begin();

  // Serial.println("[WEB] Web server started");

  // MQTT 설정 (연결은 loop()에서 처리 - setup blocking 방지)
  mqtt.setServer(mqtt_server.c_str(), mqtt_port);
  mqtt.setCallback(mqttCallback);
  mqtt.setBufferSize(1024); // Climate discovery 메시지가 길어서 버퍼 크기 증가

  // Watchdog 타이머 시작 (30초마다)
  watchdogTicker.attach(30, watchdogCallback);

  // Serial.println("=== System Ready ===\n");
}

void loop()
{
  if (!isAPMode)
  {
    ArduinoOTA.handle();
  }

  if (otaInProgress)
  {
    delay(1);
    return;
  }

  // 재부팅 예약 처리 (HTTP 응답 완료 후 안전하게 재부팅)
  if (shouldReboot && millis() - rebootScheduledTime >= 2000)
  {
    // Serial.println("[SYSTEM] Rebooting now...");
    // Serial.flush(); // 시리얼 버퍼 비우기
    delay(100);
    ESP.restart();
  }

  // 메모리 경고 (임계값 이하일 때만)
  if (millis() - lastMemoryCheck > MEMORY_CHECK_INTERVAL)
  {
    lastMemoryCheck = millis();
    uint32_t freeHeap = ESP.getFreeHeap();
    if (freeHeap < MEMORY_WARNING_THRESHOLD)
    {
      // Serial.printf("[WARNING] Low memory: %u bytes\n", freeHeap);
      if (WiFi.status() == WL_CONNECTED && mqtt.connected())
      {
        char msg[64];
        snprintf(msg, sizeof(msg), "Low memory: %u bytes", freeHeap);
        mqtt.publish("home/wallpad/log/warning", msg);
      }
    }
  }

  // RS485 활동 감시 Watchdog 활성화 (부팅 5분 후 - 초기 연결 안정화 대기)
  if (!rs485ActivityWatchdogEnabled && millis() > RS485_WATCHDOG_ENABLE_DELAY_MS)
  {
    rs485ActivityWatchdogEnabled = true;
  }

  // RS485 10분 무수신 시 재부팅 (XY-017 래치 또는 파서 비정상 복구)
  if (rs485ActivityWatchdogEnabled && !isAPMode && !otaInProgress)
  {
    if (millis() - lastRS485ActivityTime > RS485_ACTIVITY_TIMEOUT_MS)
    {
      if (WiFi.status() == WL_CONNECTED && mqtt.connected())
      {
        mqtt.publish("home/wallpad/log/warning", "RS485 silence timeout: rebooting");
        delay(300);
      }
      ESP.restart();
    }
  }

  // Watchdog 플래그 처리
  if (shouldCheckWifiConnection)
  {
    shouldCheckWifiConnection = false;
    wifiReconnectAttempts++;

    // Serial.printf("[WATCHDOG] WiFi disconnected (attempt %d/%d)\n",
    //               wifiReconnectAttempts, MAX_WIFI_RECONNECT_ATTEMPTS);

    if (wifiReconnectAttempts >= MAX_WIFI_RECONNECT_ATTEMPTS)
    {
      // Serial.println("[WATCHDOG] Max reconnect attempts reached, restarting...");
      delay(100);
      ESP.restart();
    }
    else
    {
      // WiFi 재연결 시도
      WiFi.reconnect();
    }
  }
  else
  {
    // WiFi 연결되면 카운터 리셋
    if (WiFi.status() == WL_CONNECTED && wifiReconnectAttempts > 0)
    {
      // Serial.println("[WATCHDOG] WiFi reconnected successfully");
      wifiReconnectAttempts = 0;
    }
  }

  if (shouldCheckMqttConnection)
  {
    shouldCheckMqttConnection = false;

    // AP 모드에서는 MQTT 연결 시도 안 함
    if (!isAPMode)
    {
      if (!isMonitorSessionActive())
      {
        connectMQTT();
      }
    }
  }

  // WiFi 스캔 완료 확인
  checkWiFiScanComplete();

  static unsigned long lastBinarySensorCheck = 0;
  static unsigned long lastMqttReconnect = 0;
  static bool discoveryPublished = false;
  bool monitorActive = isMonitorSessionActive();

  // MQTT 연결 유지 (AP 모드가 아니고 WiFi 연결된 경우만)
  // 부팅 직후 20초 딜레이 추가 (웹 서버 완전 정상화 후)
  if (!isAPMode && WiFi.isConnected() && !mqtt.connected() && millis() > 20000)
  {
    if (!monitorActive && millis() - lastMqttReconnect > 5000)
    {
      lastMqttReconnect = millis();
      yield();
      // Serial.println("[MQTT] Attempting connection from loop()...");
      if (connectMQTT())
      {
        discoveryPublished = false; // 재연결 시 Discovery 다시 발행 준비
      }
    }
  }

  // Discovery 발행 (MQTT 연결 후, 백그라운드에서 천천히)
  // 여러 loop 사이클에 분산하여 blocking 최소화
  static int discoveryIndex = 0;
  static unsigned long lastDiscoveryTime = 0;

  if (!discoveryPublished && mqtt.connected())
  {
    if (monitorActive)
    {
      // 모니터링 접속 중에는 웹소켓 우선, discovery는 잠시 지연
    }
    else
    {
      // 200ms마다 1개씩 발행 (비동기 느낌)
      if (millis() - lastDiscoveryTime > 200)
      {
        lastDiscoveryTime = millis();

        if (publishDiscoveryEntity(discoveryIndex))
        {
          discoveryIndex++;
          if (discoveryIndex >= 12) // 총 12개 엔티티 (Light 3 + Fan 1 + Lock 1 + Climate 4 + Binary 3)
          {
            // Serial.println("[DISCOVERY] All entities published!");
            discoveryPublished = true;
            discoveryIndex = 0;
          }
        }
        else
        {
          // 발행 실패 시 다음번에 재시도
          // Serial.printf("[DISCOVERY] Entity %d publish failed, retry next loop\n", discoveryIndex);
        }
      }
    }
  }

  if (mqtt.connected())
  {
    mqtt.loop();
  }
  yield();

  // WebSocket에서 받은 RS485 명령 처리 (loop() 컨텍스트 = SoftwareSerial 안전)
  if (wsCommandPending)
  {
    wsCommandPending = false;
    unsigned long sinceLastTx = millis() - lastRS485TxTime;
    if (sinceLastTx < RS485_TX_MIN_INTERVAL_MS)
    {
      delay(RS485_TX_MIN_INTERVAL_MS - sinceLastTx);
    }
    rs485.write(wsCommandBuffer, wsCommandLen);
    lastRS485TxTime = millis();
    // 모든 WebSocket 클라이언트에 TX 로그 브로드캐스트 (MQTT 경로와 동일하게)
    if (ws.count() > 0 && ESP.getFreeHeap() > 12000)
    {
      ws.textAll("TX: " + RS485Parser::frameToHex(wsCommandBuffer, wsCommandLen));
    }
  }

  // RS485 데이터 수신 및 파싱 (최대 50바이트/루프)
  int bytesProcessed = 0;
  while (rs485.available() && bytesProcessed < 50)
  {
    uint8_t byte = rs485.read();
    lastRS485ActivityTime = millis();
    rs485BytesSinceLastFrame++;
    // 너무 많은 바이트가 유효 프레임 없이 쌓이면 파서 리셋 (파서 상태 꼬임 방지)
    if (rs485BytesSinceLastFrame > MAX_BYTES_WITHOUT_FRAME)
    {
      parser.reset();
      rs485BytesSinceLastFrame = 0;
    }
    parser.addByte(byte);
    bytesProcessed++;

    if (bytesProcessed % 10 == 0)
    {
      yield();
    }

    // 프레임 완성 시 파싱
    if (parser.isFrameReady())
    {
      RS485Frame frame = parser.parseFrame();

      // 모니터링 가시성 확보: 유효성 여부와 관계없이 파싱된 프레임은 표시
      if (ws.count() > 0 && ESP.getFreeHeap() > 12000 && frame.rawLength > 0)
      {
        String rawHex = RS485Parser::frameToHex(frame.raw, frame.rawLength);
        if (frame.valid)
        {
          ws.textAll("RX: " + rawHex);
        }
        else
        {
          ws.textAll("RX: [INVALID] " + rawHex);
        }
      }

      if (frame.valid)
      {
        consecutiveInvalidFrames = 0;
        rs485BytesSinceLastFrame = 0;
        yield();

        // 장치 상태 업데이트 및 변경 감지
        bool stateChanged = deviceManager.processFrame(frame);

        if (stateChanged)
        {
          // JSON으로 변환하여 MQTT 발행
          String jsonState = DeviceDecoder::autoDecodeToJson(frame);
          // Serial.println("  State: " + jsonState);

          // 장치별 MQTT Topic으로 발행
          String topic = "home/wallpad/";
          switch (frame.deviceType)
          {
          case DEVICE_LIGHT:
            topic += "light/" + String(frame.deviceAddress - 0x10) + "/state";
            break;
          case DEVICE_FAN:
            topic += "fan/state";
            break;
          case DEVICE_DOORLOCK:
            topic += "doorlock/state";
            break;
          case DEVICE_CLIMATE:
            topic += "climate/" + String(frame.deviceAddress - 0x10) + "/state";
            break;
          default:
            topic += "unknown/state";
          }

          mqtt.publish(topic.c_str(), jsonState.c_str(), true);
          yield();

          // WebSocket 전송 (클라이언트 있고 메모리 충분할 때만)
          if (ws.count() > 0 && ESP.getFreeHeap() > 12000)
          {
            ws.textAll("STATE: " + jsonState);
          }
        }
      }
      else
      {
        consecutiveInvalidFrames++;
        if (consecutiveInvalidFrames >= MAX_CONSECUTIVE_INVALID)
        {
          consecutiveInvalidFrames = 0;
          rs485BytesSinceLastFrame = 0;
          if (WiFi.status() == WL_CONNECTED && mqtt.connected())
          {
            mqtt.publish("home/wallpad/log/warning", "RS485: too many invalid frames");
          }
        }
      }
    }
  }

  // Binary 센서 체크 (1초마다)
  static unsigned long lastWsPing = 0;
  if (millis() - lastBinarySensorCheck > 1000)
  {
    lastBinarySensorCheck = millis();
    if (mqtt.connected())
    {
      checkBinarySensors();
    }

    // WebSocket ping (30초마다)
    if (millis() - lastWsPing > 30000)
    {
      lastWsPing = millis();
      if (ws.count() > 0)
      {
        ws.pingAll();
        // Serial.printf("[WS] Ping sent to %u clients\n", ws.count());
      }
    }
  }

  // WebSocket 클린업 (죽은 연결 제거)
  ws.cleanupClients();
}