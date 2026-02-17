#ifndef DEVICE_DECODER_H
#define DEVICE_DECODER_H

#include <Arduino.h>
#include "rs485_parser.h"

// 조명 상태
struct LightState
{
  uint8_t address; // 조명 주소 (11, 12, 13)
  bool isOn;       // ON/OFF 상태
  bool valid;
};

// 환기 상태
struct FanState
{
  bool isOn;     // 전원 상태
  uint8_t speed; // 풍량 (0=OFF, 1=약, 3=중, 7=강)
  bool valid;
};

// 도어락 상태
struct DoorLockState
{
  bool isOpen; // 열림/닫힘
  bool valid;
};

// 난방 상태
struct ClimateState
{
  uint8_t roomAddress; // 방 주소 (11=거실, 12=방1, 13=방2, 14=방3)
  uint8_t mode;        // 1=난방, 4=OFF, 7=외출
  uint8_t currentTemp; // 현재 온도
  uint8_t targetTemp;  // 설정 온도
  bool valid;
};

// 장치 디코더 클래스
class DeviceDecoder
{
public:
  // 조명 상태 디코딩
  static LightState decodeLight(const RS485Frame &frame)
  {
    LightState state;
    state.valid = false;

    if (!frame.valid || frame.deviceType != DEVICE_LIGHT)
    {
      return state;
    }

    // 응답 프레임만 처리 (CMD_RESPONSE = 0x04)
    if (frame.command == CMD_RESPONSE && frame.dataLength >= 2)
    {
      state.address = frame.deviceAddress;
      uint8_t status = frame.data[1];
      state.isOn = (status == 0x01);
      state.valid = true;
    }

    return state;
  }

  // 환기 상태 디코딩
  static FanState decodeFan(const RS485Frame &frame)
  {
    FanState state;
    state.valid = false;

    if (!frame.valid || frame.deviceType != DEVICE_FAN)
    {
      return state;
    }

    // 응답 프레임 처리
    if (frame.command == CMD_RESPONSE && frame.dataLength >= 2)
    {
      uint8_t power = frame.data[0];
      uint8_t speed = frame.data[1];

      state.isOn = (power == 0x01);
      state.speed = speed; // 0x01=약, 0x03=중, 0x07=강
      state.valid = true;
    }

    return state;
  }

  // 도어락 상태 디코딩
  static DoorLockState decodeDoorLock(const RS485Frame &frame)
  {
    DoorLockState state;
    state.valid = false;

    if (!frame.valid || frame.deviceType != DEVICE_DOORLOCK)
    {
      return state;
    }

    if (frame.command == CMD_RESPONSE && frame.dataLength >= 1)
    {
      // 도어락 상태 (프로토콜 기준 확인 필요)
      state.isOpen = (frame.data[0] == 0x04);
      state.valid = true;
    }

    return state;
  }

  // 난방 상태 디코딩
  static ClimateState decodeClimate(const RS485Frame &frame)
  {
    ClimateState state;
    state.valid = false;

    if (!frame.valid || frame.deviceType != DEVICE_CLIMATE)
    {
      return state;
    }

    // 응답 프레임: F7 0D 01 18 04 45 RR 00 SS CC TT
    if (frame.command == CMD_RESPONSE && frame.dataLength >= 4)
    {
      state.roomAddress = frame.deviceAddress;
      state.mode = frame.data[1];        // SS: 01=난방, 04=OFF, 07=외출
      state.currentTemp = frame.data[2]; // CC: 현재 온도
      state.targetTemp = frame.data[3];  // TT: 설정 온도
      state.valid = true;
    }

    return state;
  }

  // 조명 상태를 JSON 문자열로 변환
  static String lightStateToJson(const LightState &state)
  {
    if (!state.valid)
      return "{}";

    String json = "{";
    json += "\"device\":\"light\",";
    json += "\"address\":" + String(state.address) + ",";
    json += "\"state\":\"" + String(state.isOn ? "ON" : "OFF") + "\"";
    json += "}";
    return json;
  }

  // 환기 상태를 JSON 문자열로 변환
  static String fanStateToJson(const FanState &state)
  {
    if (!state.valid)
      return "{}";

    String json = "{";
    json += "\"device\":\"fan\",";
    json += "\"state\":\"" + String(state.isOn ? "ON" : "OFF") + "\",";

    String speedStr = "OFF";
    if (state.speed == 0x01)
      speedStr = "LOW";
    else if (state.speed == 0x03)
      speedStr = "MEDIUM";
    else if (state.speed == 0x07)
      speedStr = "HIGH";

    json += "\"speed\":\"" + speedStr + "\"";
    json += "}";
    return json;
  }

  // 도어락 상태를 JSON 문자열로 변환
  static String doorLockStateToJson(const DoorLockState &state)
  {
    if (!state.valid)
      return "{}";

    String json = "{";
    json += "\"device\":\"doorlock\",";
    json += "\"state\":\"" + String(state.isOpen ? "OPEN" : "CLOSED") + "\"";
    json += "}";
    return json;
  }

  // 난방 상태를 JSON 문자열로 변환
  static String climateStateToJson(const ClimateState &state)
  {
    if (!state.valid)
      return "{}";

    String json = "{";
    json += "\"device\":\"climate\",";
    json += "\"room\":" + String(state.roomAddress) + ",";

    String modeStr = "OFF";
    if (state.mode == 0x01)
      modeStr = "HEAT";
    else if (state.mode == 0x07)
      modeStr = "AWAY";

    json += "\"mode\":\"" + modeStr + "\",";
    json += "\"current_temp\":" + String(state.currentTemp) + ",";
    json += "\"target_temp\":" + String(state.targetTemp);
    json += "}";
    return json;
  }

  // 프레임을 자동으로 디코딩하여 JSON으로 변환
  static String autoDecodeToJson(const RS485Frame &frame)
  {
    if (!frame.valid)
      return "{}";

    switch (frame.deviceType)
    {
    case DEVICE_LIGHT:
    {
      LightState state = decodeLight(frame);
      return lightStateToJson(state);
    }
    case DEVICE_FAN:
    {
      FanState state = decodeFan(frame);
      return fanStateToJson(state);
    }
    case DEVICE_DOORLOCK:
    {
      DoorLockState state = decodeDoorLock(frame);
      return doorLockStateToJson(state);
    }
    case DEVICE_CLIMATE:
    {
      ClimateState state = decodeClimate(frame);
      return climateStateToJson(state);
    }
    default:
      return "{\"device\":\"unknown\"}";
    }
  }
};

#endif
