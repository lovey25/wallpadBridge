# 개발 로드맵 (Development Roadmap)

## 🎯 다음 단계 (Next Steps)

### 1. 하드웨어 테스트

**Priority: High**

- [x] Serial Monitor로 부팅 로그 확인
- [x] WiFi 연결 및 MQTT 연결 검증
- [x] 웹 UI 접속 테스트 (http://[ESP_IP])
- [x] config.htm에서 설정 변경 및 저장 테스트
- [x] WebSocket 연결 성능 최적화 (8초 → 20ms)

### 2. RS485 실제 통신 테스트

**Priority: High**

- [x] XY-017 RS485 컨버터 연결
- [x] 월패드 RS485 라인 연결 (12V 전원 주의)
- [x] /monitor.htm에서 실시간 패킷 모니터링
- [x] 수신 패킷 파싱 검증
  - Light 상태 변경 확인
  - Fan 속도 변경 확인
  - Climate 온도/모드 변경 확인
  - DoorLock 상태 확인
- [ ] MQTT → RS485 명령 송신 테스트
  - Home Assistant에서 조명 켜기/끄기
  - 환기팬 속도 변경
  - 온도 설정 변경
- [ ] 프로토콜 문서 업데이트 (실제 데이터 기반)

### 3. Home Assistant 통합 검증

**Priority: Medium**

- [x] MQTT Discovery 자동 등록 확인
  - Light 엔티티 3개 (거실조명1, 거실조명2, 3Way 조명)
  - Fan 엔티티
  - Door Lock 엔티티
  - Climate 엔티티 4개 (거실, 방1, 방2, 방3)
  - Binary Sensor 엔티티 3개
- [x] Discovery 비동기 발행으로 성능 개선 (200ms 간격)
- [x] Climate 엔티티 온도 조절기 설정 수정
  - temperature_unit, precision 필드 추가
  - Mode 소문자 변경 (heat, off)
- [ ] Home Assistant UI에서 장치 제어 테스트 (실제 RS485 필요)
- [ ] LWT(Last Will Testament) 동작 확인
- [ ] 상태 동기화 검증 (RS485 → MQTT)

---

## 🚀 향후 개발 과제

### Phase 1: 보안 및 안정성

**Priority: Medium**

- [ ] **웹 UI 인증**
  - HTTP Basic Auth 구현
  - 관리자 비밀번호 설정 UI
  - 세션 타임아웃
- [ ] **MQTT over TLS**
  - 인증서 관리
  - 보안 연결 옵션
- [ ] **설정 백업/복구**
  - config.json 내보내기/가져오기
  - 공장 초기화 API (`/factory-reset`)

### Phase 2: OTA 펌웨어 업데이트

**Priority: Medium**

- [x] ArduinoOTA 통합 (WiFi STA 모드 자동 활성화)
- [x] OTA 호스트명/비밀번호 매크로 지원 (`OTA_HOSTNAME`, `OTA_PASSWORD`)
- [ ] 업데이트 실패 시 롤백 메커니즘

---

## 📝 알려진 제한사항

- **메모리**: ESP8266 제한으로 동시 처리 가능한 장치 수 제한
- **RS485**: SoftwareSerial 사용으로 9600bps 제한
- **WebSocket**: 동시 연결 클라이언트 수 제한 (최대 3개)
- **LittleFS**: 256KB 파일시스템 용량

## ✅ 최근 해결된 이슈

### 2026-02-18: 성능 최적화

- **WebSocket 연결 지연 (8초)**: monitor.htm의 script를 head → body 끝으로 이동하여 해결
- **WiFi 스캔 느림 (10초+)**: Hidden SSID 스캔 제외로 해결
- **MQTT Discovery blocking**: 비동기 분산 발행으로 개선
- **Climate 엔티티 표시 오류**: Discovery 설정 수정 및 mode 소문자 변경

---

## 🔧 개선 아이디어

- ESP32로 포팅 (더 많은 메모리, 하드웨어 UART)
- Captive Portal 개선 (WiFi 설정 시 자동 팝업)
- 다국어 지원 (영어, 한국어)
- 다크/라이트 테마 전환
- 모바일 반응형 UI 개선

---

## 📚 참고 문서

- [README.md](./README.md) - 프로젝트 개요 및 기능 설명
- [wallpad_protocol.md](./wallpad_protocol.md) - RS485 통신 프로토콜
- [PlatformIO Documentation](https://docs.platformio.org/)
- [Home Assistant MQTT Discovery](https://www.home-assistant.io/docs/mqtt/discovery/)
- [ESPAsyncWebServer](https://github.com/me-no-dev/ESPAsyncWebServer)
