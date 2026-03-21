#ifndef COMMAND_BUILDER_H
#define COMMAND_BUILDER_H

#include <Arduino.h>
#include "checksum.h"

// RS485 명령 프레임 생성기
class CommandBuilder
{
public:
  // 조명 상태 조회 명령 생성
  static size_t buildLightQuery(uint8_t address, uint8_t *buffer, size_t bufferSize)
  {
    // REQ: F7 0B 01 19 01 40 AA 00 [CRC] EE
    if (bufferSize < 11)
      return 0;

    buffer[0] = 0xF7;                            // Prefix
    buffer[1] = 0x0B;                            // Length
    buffer[2] = 0x01;                            // Fixed
    buffer[3] = 0x19;                            // Device: Light
    buffer[4] = 0x01;                            // Command: Query
    buffer[5] = 0x40;                            // Fixed
    buffer[6] = address;                         // Light address
    buffer[7] = 0x00;                            // Data
    buffer[8] = 0x00;                            // Reserved/Padding
    buffer[9] = Checksum::xorSum(&buffer[1], 8); // Checksum
    buffer[10] = 0xEE;                           // Suffix

    return 11;
  }

  // 조명 제어 명령 생성 (ON/OFF)
  static size_t buildLightControl(uint8_t address, bool turnOn, uint8_t *buffer, size_t bufferSize)
  {
    // ON  REQ: F7 0B 01 19 02 40 AA 01 [CRC] EE
    // OFF REQ: F7 0B 01 19 02 40 AA 02 [CRC] EE
    if (bufferSize < 11)
      return 0;

    buffer[0] = 0xF7;                            // Prefix
    buffer[1] = 0x0B;                            // Length
    buffer[2] = 0x01;                            // Fixed
    buffer[3] = 0x19;                            // Device: Light
    buffer[4] = 0x02;                            // Command: Set
    buffer[5] = 0x40;                            // Fixed
    buffer[6] = address;                         // Light address
    buffer[7] = turnOn ? 0x01 : 0x02;            // 01=ON, 02=OFF
    buffer[8] = 0x00;                            // Reserved/Padding
    buffer[9] = Checksum::xorSum(&buffer[1], 8); // Checksum
    buffer[10] = 0xEE;                           // Suffix

    return 11;
  }

  // 환기 상태 조회 명령 생성
  static size_t buildFanQuery(uint8_t *buffer, size_t bufferSize)
  {
    // REQ: F7 0B 01 2B 01 40 11 00 [CRC] EE
    if (bufferSize < 11)
      return 0;

    buffer[0] = 0xF7;                            // Prefix
    buffer[1] = 0x0B;                            // Length
    buffer[2] = 0x01;                            // Fixed
    buffer[3] = 0x2B;                            // Device: Fan
    buffer[4] = 0x01;                            // Command: Query
    buffer[5] = 0x40;                            // Fixed
    buffer[6] = 0x11;                            // Fan address
    buffer[7] = 0x00;                            // Data
    buffer[8] = 0x00;                            // Reserved/Padding
    buffer[9] = Checksum::xorSum(&buffer[1], 8); // Checksum
    buffer[10] = 0xEE;                           // Suffix

    return 11;
  }

  // 환기 전원 제어 명령 생성
  static size_t buildFanPowerControl(bool turnOn, uint8_t *buffer, size_t bufferSize)
  {
    // OFF REQ: F7 0B 01 2B 02 40 11 02 [CRC] EE
    // ON  REQ: F7 0B 01 2B 02 42 11 01 [CRC] EE
    if (bufferSize < 11)
      return 0;

    buffer[0] = 0xF7;                            // Prefix
    buffer[1] = 0x0B;                            // Length
    buffer[2] = 0x01;                            // Fixed
    buffer[3] = 0x2B;                            // Device: Fan
    buffer[4] = 0x02;                            // Command: Set
    buffer[5] = turnOn ? 0x42 : 0x40;            // 42=ON, 40=OFF
    buffer[6] = 0x11;                            // Fan address
    buffer[7] = turnOn ? 0x01 : 0x02;            // 01=ON, 02=OFF
    buffer[8] = 0x00;                            // Reserved/Padding
    buffer[9] = Checksum::xorSum(&buffer[1], 8); // Checksum
    buffer[10] = 0xEE;                           // Suffix

    return 11;
  }

  // 환기 풍량 제어 명령 생성
  static size_t buildFanSpeedControl(uint8_t speed, uint8_t *buffer, size_t bufferSize)
  {
    // LOW  : F7 0B 01 2B 02 42 11 01 [CRC] EE
    // MED  : F7 0B 01 2B 02 42 11 03 [CRC] EE
    // HIGH : F7 0B 01 2B 02 42 11 07 [CRC] EE
    if (bufferSize < 11)
      return 0;

    // speed: 1=LOW, 3=MEDIUM, 7=HIGH
    if (speed != 1 && speed != 3 && speed != 7)
    {
      speed = 1; // 기본값: LOW
    }

    buffer[0] = 0xF7;                            // Prefix
    buffer[1] = 0x0B;                            // Length
    buffer[2] = 0x01;                            // Fixed
    buffer[3] = 0x2B;                            // Device: Fan
    buffer[4] = 0x02;                            // Command: Set
    buffer[5] = 0x42;                            // Fixed (ON 상태)
    buffer[6] = 0x11;                            // Fan address
    buffer[7] = speed;                           // Speed value
    buffer[8] = 0x00;                            // Reserved/Padding
    buffer[9] = Checksum::xorSum(&buffer[1], 8); // Checksum
    buffer[10] = 0xEE;                           // Suffix

    return 11;
  }

  // 도어락 열림 명령 생성
  static size_t buildDoorLockOpen(uint8_t *buffer, size_t bufferSize)
  {
    // REQ: F7 0E 01 1E 02 43 11 04 00 04 FF FF [CRC] EE
    if (bufferSize < 14)
      return 0;

    buffer[0] = 0xF7;                              // Prefix
    buffer[1] = 0x0E;                              // Length
    buffer[2] = 0x01;                              // Fixed
    buffer[3] = 0x1E;                              // Device: DoorLock
    buffer[4] = 0x02;                              // Command: Set
    buffer[5] = 0x43;                              // Fixed
    buffer[6] = 0x11;                              // DoorLock address
    buffer[7] = 0x04;                              // Open command
    buffer[8] = 0x00;                              // Data
    buffer[9] = 0x04;                              // Data
    buffer[10] = 0xFF;                             // Data
    buffer[11] = 0xFF;                             // Data
    buffer[12] = Checksum::xorSum(&buffer[1], 11); // Checksum
    buffer[13] = 0xEE;                             // Suffix

    return 14;
  }

  // 난방 상태 조회 명령 생성
  static size_t buildClimateQuery(uint8_t roomAddress, uint8_t *buffer, size_t bufferSize)
  {
    // REQ: F7 0B 01 18 01 45 RR 00 [CRC] EE
    if (bufferSize < 11)
      return 0;

    buffer[0] = 0xF7;                            // Prefix
    buffer[1] = 0x0B;                            // Length
    buffer[2] = 0x01;                            // Fixed
    buffer[3] = 0x18;                            // Device: Climate
    buffer[4] = 0x01;                            // Command: Query
    buffer[5] = 0x45;                            // Fixed
    buffer[6] = roomAddress;                     // Room address (11=거실, 12=방1, 13=방2, 14=방3)
    buffer[7] = 0x00;                            // Data
    buffer[8] = 0x00;                            // Reserved/Padding
    buffer[9] = Checksum::xorSum(&buffer[1], 8); // Checksum
    buffer[10] = 0xEE;                           // Suffix

    return 11;
  }

  // 난방 모드 제어 명령 생성
  static size_t buildClimateModeControl(uint8_t roomAddress, uint8_t mode, uint8_t *buffer, size_t bufferSize)
  {
    // HEAT REQ: F7 0B 01 18 02 46 RR 01 [CRC] EE
    // OFF  REQ: F7 0B 01 18 02 46 RR 04 [CRC] EE
    // AWAY REQ: F7 0B 01 18 02 46 RR 07 [CRC] EE
    if (bufferSize < 11)
      return 0;

    // mode: 1=HEAT, 4=OFF, 7=AWAY
    if (mode != 1 && mode != 4 && mode != 7)
    {
      mode = 4; // 기본값: OFF
    }

    buffer[0] = 0xF7;                            // Prefix
    buffer[1] = 0x0B;                            // Length
    buffer[2] = 0x01;                            // Fixed
    buffer[3] = 0x18;                            // Device: Climate
    buffer[4] = 0x02;                            // Command: Set
    buffer[5] = 0x46;                            // Fixed
    buffer[6] = roomAddress;                     // Room address
    buffer[7] = mode;                            // Mode value
    buffer[8] = 0x00;                            // Reserved/Padding
    buffer[9] = Checksum::xorSum(&buffer[1], 8); // Checksum
    buffer[10] = 0xEE;                           // Suffix

    return 11;
  }

  // 난방 온도 설정 명령 생성
  static size_t buildClimateTempControl(uint8_t roomAddress, uint8_t targetTemp, uint8_t *buffer, size_t bufferSize)
  {
    // REQ: F7 0B 01 18 02 45 RR TT [CRC] EE
    if (bufferSize < 11)
      return 0;

    // 온도 범위 제한 (5~40도)
    if (targetTemp < 5)
      targetTemp = 5;
    if (targetTemp > 40)
      targetTemp = 40;

    buffer[0] = 0xF7;                            // Prefix
    buffer[1] = 0x0B;                            // Length
    buffer[2] = 0x01;                            // Fixed
    buffer[3] = 0x18;                            // Device: Climate
    buffer[4] = 0x02;                            // Command: Set
    buffer[5] = 0x45;                            // Fixed
    buffer[6] = roomAddress;                     // Room address
    buffer[7] = targetTemp;                      // Target temperature
    buffer[8] = 0x00;                            // Reserved/Padding
    buffer[9] = Checksum::xorSum(&buffer[1], 8); // Checksum
    buffer[10] = 0xEE;                           // Suffix

    return 11;
  }

  // 버퍼를 Hex 문자열로 변환 (디버깅용)
  static String toHexString(const uint8_t *buffer, size_t length)
  {
    String result = "";
    for (size_t i = 0; i < length; i++)
    {
      if (buffer[i] < 0x10)
        result += "0";
      result += String(buffer[i], HEX);
      if (i < length - 1)
        result += " ";
    }
    result.toUpperCase();
    return result;
  }
};

#endif
