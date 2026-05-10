# 개발 로드맵 (Development Roadmap)

> 완료된 기능 목록은 [README.md](./README.md) 7절 참조

---

## 🎯 미완료 항목 (Pending)

### RS485 통신 테스트

- [ ] 신규 프레임 발견 시 `wallpad_protocol.md` 보완 (실수신 데이터 기반)

### Home Assistant 통합 검증

- [ ] LWT(Last Will Testament) 동작 확인 (하드웨어 연결 후 실환경 검증 필요)
- [ ] 상태 동기화 검증 (RS485 → MQTT, 하드웨어 연결 후 실환경 검증 필요)

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
  - config.json 내보내기 (`/api/config` GET 이미 일부 구현됨)
  - 공장 초기화 API (`/factory-reset`)

### Phase 2: OTA 안정성

**Priority: Medium**

- [ ] OTA 업데이트 실패 시 롤백 메커니즘

---

## 🔧 개선 아이디어

- ESP32로 포팅 (더 많은 메모리, 하드웨어 UART)
- Captive Portal 개선 (WiFi 설정 시 자동 팝업)
- 다국어 지원 (영어, 한국어)
- 다크/라이트 테마 전환
- 모바일 반응형 UI 개선
- Fan 속도 변경 MQTT 토픽 전용화 (`home/wallpad/fan/speed/set`)

---

## 📚 참고 문서

- [README.md](./README.md) - 프로젝트 개요 및 기능 설명
- [wallpad_protocol.md](./wallpad_protocol.md) - RS485 통신 프로토콜
- [PlatformIO Documentation](https://docs.platformio.org/)
- [Home Assistant MQTT Discovery](https://www.home-assistant.io/docs/mqtt/discovery/)
- [ESPAsyncWebServer](https://github.com/me-no-dev/ESPAsyncWebServer)
