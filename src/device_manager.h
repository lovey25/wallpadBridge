#ifndef DEVICE_MANAGER_H
#define DEVICE_MANAGER_H

#include <Arduino.h>
#include "device_decoder.h"
#include <map>

// 장치별 상태 저장 및 변경 감지를 위한 관리자
class DeviceManager
{
public:
  DeviceManager() {}

  // 조명 상태 업데이트 및 변경 여부 확인
  bool updateLight(uint8_t address, const LightState &newState)
  {
    if (!newState.valid)
      return false;

    String key = "light_" + String(address);

    // 이전 상태와 비교
    if (lightStates.find(key) != lightStates.end())
    {
      LightState &oldState = lightStates[key];
      if (oldState.isOn != newState.isOn)
      {
        lightStates[key] = newState;
        return true; // 상태 변경됨
      }
      return false; // 상태 변경 없음
    }

    // 새로운 장치
    lightStates[key] = newState;
    return true;
  }

  // 환기 상태 업데이트
  bool updateFan(const FanState &newState)
  {
    if (!newState.valid)
      return false;

    if (fanState.valid)
    {
      // 상태 변경 감지
      if (fanState.isOn != newState.isOn || fanState.speed != newState.speed)
      {
        fanState = newState;
        return true;
      }
      return false;
    }

    fanState = newState;
    return true;
  }

  // 도어락 상태 업데이트
  bool updateDoorLock(const DoorLockState &newState)
  {
    if (!newState.valid)
      return false;

    if (doorLockState.valid)
    {
      if (doorLockState.isOpen != newState.isOpen)
      {
        doorLockState = newState;
        return true;
      }
      return false;
    }

    doorLockState = newState;
    return true;
  }

  // 난방 상태 업데이트
  bool updateClimate(uint8_t roomAddress, const ClimateState &newState)
  {
    if (!newState.valid)
      return false;

    String key = "climate_" + String(roomAddress);

    if (climateStates.find(key) != climateStates.end())
    {
      ClimateState &oldState = climateStates[key];
      // 모드, 현재 온도, 설정 온도 중 하나라도 변경되었는지 확인
      if (oldState.mode != newState.mode ||
          oldState.currentTemp != newState.currentTemp ||
          oldState.targetTemp != newState.targetTemp)
      {
        climateStates[key] = newState;
        return true;
      }
      return false;
    }

    climateStates[key] = newState;
    return true;
  }

  // 현재 상태 조회
  LightState getLightState(uint8_t address)
  {
    String key = "light_" + String(address);
    if (lightStates.find(key) != lightStates.end())
    {
      return lightStates[key];
    }
    LightState invalid;
    invalid.valid = false;
    return invalid;
  }

  FanState getFanState()
  {
    return fanState;
  }

  DoorLockState getDoorLockState()
  {
    return doorLockState;
  }

  ClimateState getClimateState(uint8_t roomAddress)
  {
    String key = "climate_" + String(roomAddress);
    if (climateStates.find(key) != climateStates.end())
    {
      return climateStates[key];
    }
    ClimateState invalid;
    invalid.valid = false;
    return invalid;
  }

  // 모든 상태를 JSON 배열로 반환 (디버깅/대시보드용)
  String getAllStatesJson()
  {
    String json = "[";
    bool first = true;

    // 조명 상태들
    for (auto &pair : lightStates)
    {
      if (!first)
        json += ",";
      json += DeviceDecoder::lightStateToJson(pair.second);
      first = false;
    }

    // 환기 상태
    if (fanState.valid)
    {
      if (!first)
        json += ",";
      json += DeviceDecoder::fanStateToJson(fanState);
      first = false;
    }

    // 도어락 상태
    if (doorLockState.valid)
    {
      if (!first)
        json += ",";
      json += DeviceDecoder::doorLockStateToJson(doorLockState);
      first = false;
    }

    // 난방 상태들
    for (auto &pair : climateStates)
    {
      if (!first)
        json += ",";
      json += DeviceDecoder::climateStateToJson(pair.second);
      first = false;
    }

    json += "]";
    return json;
  }

  // 프레임을 처리하고 상태 변경 여부 반환
  bool processFrame(const RS485Frame &frame)
  {
    if (!frame.valid)
      return false;

    bool changed = false;

    switch (frame.deviceType)
    {
    case DEVICE_LIGHT:
    {
      LightState state = DeviceDecoder::decodeLight(frame);
      changed = updateLight(state.address, state);
      break;
    }
    case DEVICE_FAN:
    {
      FanState state = DeviceDecoder::decodeFan(frame);
      changed = updateFan(state);
      break;
    }
    case DEVICE_DOORLOCK:
    {
      DoorLockState state = DeviceDecoder::decodeDoorLock(frame);
      changed = updateDoorLock(state);
      break;
    }
    case DEVICE_CLIMATE:
    {
      ClimateState state = DeviceDecoder::decodeClimate(frame);
      changed = updateClimate(state.roomAddress, state);
      break;
    }
    }

    return changed;
  }

private:
  std::map<String, LightState> lightStates;
  std::map<String, ClimateState> climateStates;
  FanState fanState = {false, 0, false};
  DoorLockState doorLockState = {false, false};
};

#endif
