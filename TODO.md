# 개발 로드맵 (Development Roadmap)

> 완료된 기능 목록은 [README.md](./README.md) 7절 참조

---

## 🖥️ 모니터 UI 개선 (진행 중)

**우선순위: High** — 쉬운 것부터 단계별 적용

- [x] **Step 1**: 시간 포맷 HHMMSS 변환 — `addLog()` `[HH:MM:SS]` 고정 2자리 포맷
- [x] **Step 2**: PC 2분할 레이아웃 + 전체화면 — 좌측 컨트롤 / 우측 로그 콘솔, body 여백 최소화
- [x] **Step 3**: 알려진 코드 숨기기 — `knownCodes[]` + 토글, `localStorage` 영속
- [x] **Step 4**: HEX 바이트 필터 — 바이트 위치 + HEX 값 + 일치/불일치 룰, `localStorage` 영속
- [x] **Step 5**: HEX → 십진수 분석 — 필드 위치+레이블 지정 → 로그 뒤에 `[레이블: N]` 자동 표시

---

## 🎯 미완료 항목 (Pending)

### RS485 통신 테스트

- [ ] 프로토콜 문서 업데이트 (실제 수신 데이터 기반으로 wallpad_protocol.md 보완)

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
