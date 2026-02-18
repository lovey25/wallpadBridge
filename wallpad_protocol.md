# RS485 통신 규약 명세서

## 1. 물리/링크 계층 설정

| 항목             | 값              |
| ---------------- | --------------- |
| Baud Rate        | 9600            |
| Data Bits        | 8               |
| Parity           | None            |
| Stop Bits        | 1               |
| RX Frame Timeout | 10 ms           |
| TX Interval      | 50 ms           |
| ACK Timeout      | 50 ms           |
| ACK Retry        | 1               |
| Prefix           | `F7`            |
| Suffix           | `EE`            |
| Checksum         | XOR (Checksum8) |

### Checksum 알고리즘

```cpp
crc = 0xF7
for byte in payload:
    crc ^= byte
return crc
```

---

## 2. 공통 프레임 구조

```.
[Prefix] [Length] [Device] [Command] [Data...] [Checksum] [Suffix]
```

| 필드     | 설명                    |
| -------- | ----------------------- |
| Prefix   | 프레임 시작 (`F7`)      |
| Length   | Prefix 제외 데이터 길이 |
| Device   | 장치 타입 + 주소        |
| Command  | 동작 코드               |
| Data     | 상태값 또는 설정값      |
| Checksum | XOR 결과                |
| Suffix   | 프레임 종료 (`EE`)      |

---

## 3. 주소 체계

| 구분      | 값   |
| --------- | ---- |
| 거실조명1 | `11` |
| 거실조명2 | `12` |
| 3Way 조명 | `13` |
| 환기      | `11` |
| 거실 난방 | `11` |
| 방1 난방  | `12` |
| 방2 난방  | `13` |
| 방3 난방  | `14` |

---

## 4. 장치별 프로토콜

### 4.1 조명 (Binary Light)

#### 상태 조회

```.
REQ: F7 0B 01 19 01 40 AA 00 00
RES: F7 0B 01 19 04 40 AA 00 SS
```

| 값      | 의미      |
| ------- | --------- |
| `AA`    | 조명 주소 |
| `SS=01` | ON        |
| `SS=02` | OFF       |

#### 제어

```.
ON  REQ: F7 0B 01 19 02 40 AA 01 00
ON  RES: F7 0B 01 19 04 40 AA 01 01

OFF REQ: F7 0B 01 19 02 40 AA 02 00
OFF RES: F7 0B 01 19 04 40 AA 02 02
```

---

### 4.2 환기장치 (Fan)

#### 상태 조회

```.
REQ: F7 0B 01 2B 01 40 11 00 00
RES: F7 0C 01 2B 04 40 11 00 PS
```

| 필드 | 값   | 의미 |
| ---- | ---- | ---- |
| 전원 | `02` | OFF  |
| 전원 | `01` | ON   |
| 풍량 | `01` | 약   |
| 풍량 | `03` | 중   |
| 풍량 | `07` | 강   |

#### 전원 제어

```.
OFF REQ: F7 0B 01 2B 02 40 11 02 00
ON  REQ: F7 0B 01 2B 02 42 11 01 00
```

#### 풍량 제어

```.
LOW  : F7 0B 01 2B 02 42 11 01 00
MED  : F7 0B 01 2B 02 42 11 03 00
HIGH : F7 0B 01 2B 02 42 11 07 00
```

---

### 4.3 도어락

#### 열림

```.
REQ: F7 0E 01 1E 02 43 11 04 00 04 FF FF
RES: F7 0C 01 1E 04 43 11 04 00 04
```

---

### 4.4 난방 (Climate)

#### 상태 조회

```.
REQ: F7 0B 01 18 01 45 RR 00 00
RES: F7 0D 01 18 04 45 RR 00 SS CC TT
```

| 필드    | 의미      |
| ------- | --------- |
| `RR`    | 룸 주소   |
| `SS=01` | 난방      |
| `SS=04` | OFF       |
| `SS=07` | 외출      |
| `CC`    | 현재 온도 |
| `TT`    | 설정 온도 |

#### 전원/모드 제어

```.
HEAT REQ: F7 0B 01 18 02 46 RR 01 00
OFF  REQ: F7 0B 01 18 02 46 RR 04 00
AWAY REQ: F7 0B 01 18 02 46 RR 07 00
```

#### 온도 설정

```.
REQ: F7 0B 01 18 02 45 RR TT 00
RES: F7 0D 01 18 04 45 RR TT 01 CC TT
```

---

## 5. 상태 코드 요약

| 구분       | 값             | 의미        |
| ---------- | -------------- | ----------- |
| Power ON   | `01`           | 동작        |
| Power OFF  | `02` 또는 `04` | 장치별 상이 |
| Away       | `07`           | 외출 모드   |
| Fan Low    | `01`           | 약          |
| Fan Medium | `03`           | 중          |
| Fan High   | `07`           | 강          |

---

## 6. 설계 특징

- 모든 프레임은 **Prefix(F7) / Suffix(EE)** 구조 사용
- **XOR Checksum** 기반 단순 무결성 검증
- **요청 → ACK 응답 구조**
- 장치 타입별 **Command 코드 분리**
- 온도/풍량 등은 **단일 바이트 파라미터 방식**
