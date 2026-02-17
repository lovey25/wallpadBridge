#ifndef CHECKSUM_H
#define CHECKSUM_H

#include <Arduino.h>

class Checksum
{
public:
  // 일반적인 XOR 체크섬 (대부분의 국산 월패드 방식)
  static uint8_t xorSum(const uint8_t *data, size_t len)
  {
    uint8_t crc = 0;
    for (size_t i = 0; i < len; i++)
      crc ^= data[i];
    return crc;
  }

  // 산술 합 체크섬 (일부 삼성/현대 방식)
  static uint8_t addSum(const uint8_t *data, size_t len)
  {
    uint8_t sum = 0;
    for (size_t i = 0; i < len; i++)
      sum += data[i];
    return sum;
  }
};

#endif