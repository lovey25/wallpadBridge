#ifndef RS485_PARSER_H
#define RS485_PARSER_H

#include <Arduino.h>
#include "checksum.h"

// 프레임 상수
#define FRAME_PREFIX 0xF7
#define FRAME_SUFFIX 0xEE
#define MIN_FRAME_SIZE 7 // Prefix + Length + Device + Cmd + Data(min 1) + Checksum + Suffix

// 장치 타입
enum DeviceType
{
  DEVICE_UNKNOWN = 0x00,
  DEVICE_LIGHT = 0x19,
  DEVICE_FAN = 0x2B,
  DEVICE_DOORLOCK = 0x1E,
  DEVICE_CLIMATE = 0x18
};

// 명령 타입
enum CommandType
{
  CMD_QUERY = 0x01,
  CMD_SET = 0x02,
  CMD_RESPONSE = 0x04
};

// 파싱된 프레임 구조체
struct RS485Frame
{
  uint8_t prefix;
  uint8_t length;
  uint8_t deviceType;
  uint8_t command;
  uint8_t subCommand; // buffer[5]: 동일 deviceType 내 sub-protocol 구분 (예: 0x40 broadcast vs 0x43 control ack)
  uint8_t deviceAddress;
  uint8_t data[16]; // 최대 16바이트 데이터
  uint8_t dataLength;
  uint8_t checksum;
  uint8_t suffix;
  uint8_t raw[32];
  uint8_t rawLength;
  bool lengthValid;
  bool checksumValid;
  bool valid;
};

// RS485 프레임 파서 클래스
class RS485Parser
{
public:
  RS485Parser() : bufferIndex(0), frameReady(false)
  {
    memset(buffer, 0, sizeof(buffer));
  }

  // 바이트 단위로 수신 데이터 추가
  void addByte(uint8_t byte)
  {
    // 버퍼가 가득 찬 경우 리셋
    if (bufferIndex >= MAX_BUFFER_SIZE)
    {
      reset();
    }

    // Prefix 감지 시 버퍼 리셋
    if (byte == FRAME_PREFIX && bufferIndex > 0)
    {
      // 기존 불완전한 프레임 버리고 새로 시작
      bufferIndex = 0;
    }

    buffer[bufferIndex++] = byte;

    // Suffix 감지 시 프레임 완성 여부 확인
    if (byte == FRAME_SUFFIX && bufferIndex >= MIN_FRAME_SIZE)
    {
      frameReady = true;
    }
  }

  // 완전한 프레임이 수신되었는지 확인
  bool isFrameReady()
  {
    return frameReady;
  }

  // 프레임 파싱 및 검증
  RS485Frame parseFrame()
  {
    RS485Frame frame;
    memset(&frame, 0, sizeof(frame));
    frame.valid = false;

    if (!frameReady || bufferIndex < MIN_FRAME_SIZE)
    {
      reset();
      return frame;
    }

    // 원본 프레임 보존 (디버깅/모니터링용)
    frame.rawLength = (bufferIndex > 32) ? 32 : bufferIndex;
    memcpy(frame.raw, buffer, frame.rawLength);

    // 기본 구조 검증
    if (buffer[0] != FRAME_PREFIX || buffer[bufferIndex - 1] != FRAME_SUFFIX)
    {
      reset();
      return frame;
    }

    // 필드 추출
    frame.prefix = buffer[0];
    frame.length = buffer[1];
    frame.deviceType = buffer[3];
    frame.command = buffer[4];
    frame.subCommand = (bufferIndex >= 6) ? buffer[5] : 0x00;

    // Length 검증
    // 프로토콜 문서 기준 length는 프레임 전체 길이(예: 0x0B=11, 0x0D=13)로 사용됨.
    // 기존 구현과의 호환을 위해 (전체 길이-2) 포맷도 허용한다.
    bool lengthMatchesTotal = (frame.length == bufferIndex);
    bool lengthMatchesLegacy = (frame.length == bufferIndex - 2);
    frame.lengthValid = (lengthMatchesTotal || lengthMatchesLegacy);
    if (!frame.lengthValid)
    {
      reset();
      return frame;
    }

    // 장치 주소 및 데이터 추출
    if (bufferIndex >= 7)
    {
      // 공통 포맷: [..][Cmd][Func][Addr][Data...][Checksum][Suffix]
      frame.deviceAddress = buffer[6];

      // 데이터 길이 계산: 전체 - (Prefix + Length + Fixed + Device + Cmd + Func + Addr + Checksum + Suffix)
      int dataLen = bufferIndex - 9;
      if (dataLen > 0 && dataLen <= 16)
      {
        frame.dataLength = dataLen;
        memcpy(frame.data, &buffer[7], dataLen);
      }
      else
      {
        frame.dataLength = 0;
      }
    }

    frame.checksum = buffer[bufferIndex - 2];
    frame.suffix = buffer[bufferIndex - 1];

    // Checksum 검증 (Prefix 제외, Checksum과 Suffix 제외)
    uint8_t calculatedChecksum = Checksum::xorSum(&buffer[1], bufferIndex - 3);
    // 레거시(초기값 0x00) 프레임과의 호환을 위해 Prefix xor 이전값도 허용
    uint8_t calculatedChecksumLegacy = (uint8_t)(FRAME_PREFIX ^ calculatedChecksum);

    frame.checksumValid =
        (calculatedChecksum == frame.checksum) ||
        (calculatedChecksumLegacy == frame.checksum);

    if (frame.checksumValid)
    {
      frame.valid = true;
    }

    reset();
    return frame;
  }

  // 버퍼 리셋
  void reset()
  {
    bufferIndex = 0;
    frameReady = false;
    memset(buffer, 0, sizeof(buffer));
  }

  // 프레임을 Hex 문자열로 변환 (디버깅용)
  static String frameToHex(const uint8_t *data, size_t len)
  {
    String result = "";
    for (size_t i = 0; i < len; i++)
    {
      if (data[i] < 0x10)
        result += "0";
      result += String(data[i], HEX);
      if (i < len - 1)
        result += " ";
    }
    result.toUpperCase();
    return result;
  }

  // 디바이스 타입을 문자열로 변환
  static String deviceTypeToString(uint8_t deviceType)
  {
    switch (deviceType)
    {
    case DEVICE_LIGHT:
      return "Light";
    case DEVICE_FAN:
      return "Fan";
    case DEVICE_DOORLOCK:
      return "DoorLock";
    case DEVICE_CLIMATE:
      return "Climate";
    default:
      return "Unknown";
    }
  }

  // 명령 타입을 문자열로 변환
  static String commandTypeToString(uint8_t command)
  {
    switch (command)
    {
    case CMD_QUERY:
      return "Query";
    case CMD_SET:
      return "Set";
    case CMD_RESPONSE:
      return "Response";
    default:
      return "Unknown";
    }
  }

private:
  static const int MAX_BUFFER_SIZE = 32;
  uint8_t buffer[MAX_BUFFER_SIZE];
  int bufferIndex;
  bool frameReady;
};

#endif
