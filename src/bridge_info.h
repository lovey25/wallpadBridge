#ifndef BRIDGE_INFO_H
#define BRIDGE_INFO_H

// ─────────────────────────────────────────────────────────────
// Wallpad Bridge 본체 메타정보 (Home Assistant MQTT Discovery용)
// 모든 항목은 credentials.h 또는 build flag(-D)로 override 가능.
// 값을 변경하면 다음 기동 시 HA Discovery가 새 device 정보를 발행한다.
// ─────────────────────────────────────────────────────────────

#ifndef BRIDGE_IDENTIFIER
#define BRIDGE_IDENTIFIER "wallpad_bridge" // HA device 그룹 식별자 (변경 시 HA에서 별도 디바이스로 인식)
#endif

#ifndef BRIDGE_NAME
#define BRIDGE_NAME "Wallpad Bridge" // HA UI 표시 이름
#endif

#ifndef BRIDGE_MODEL
#define BRIDGE_MODEL "EX-WPB1" // 모델명
#endif

#ifndef BRIDGE_MANUFACTURER
#define BRIDGE_MANUFACTURER "EveryX" // 제조사/제작자
#endif

#ifndef BRIDGE_SW_VERSION
#define BRIDGE_SW_VERSION "1.1.0" // 펌웨어 버전
#endif

// ─────────────────────────────────────────────────────────────
// HA Discovery 페이로드의 "device" 필드 (선두 콤마 포함, flash에 상주)
// 추가 필드(hw_version, configuration_url 등)가 필요하면 아래 매크로에 한 줄 추가.
// ─────────────────────────────────────────────────────────────
#define HA_DEVICE_INFO_JSON                       \
  ",\"device\":{"                                 \
  "\"identifiers\":[\"" BRIDGE_IDENTIFIER "\"]"   \
  ",\"name\":\"" BRIDGE_NAME "\""                 \
  ",\"model\":\"" BRIDGE_MODEL "\""               \
  ",\"manufacturer\":\"" BRIDGE_MANUFACTURER "\"" \
  ",\"sw_version\":\"" BRIDGE_SW_VERSION "\""     \
  "}"

#endif
