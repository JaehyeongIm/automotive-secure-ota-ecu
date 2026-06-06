# ISS-CAN-006: CAN Bus-Off 자동복구(ABOM) 미설정 — 한 노드 플래시 후 버스 영구 정지

> 트러블슈팅(8D 경량판). on-target 테스트 실행 중 발견.

| 항목 | 내용 |
|---|---|
| 증상 | 두 ECU를 동시에 리셋하면 CAN 정상. 그러나 `hil_runner --setup`으로 한 ECU만 플래시하면 그 뒤 CAN 통신 불가 |
| 상태 | ✅ 해결됨 (양 ECU ABOM ENABLE) · on-target 재확인 후속 |
| 심각도 | High (테스트 차단 + 실차 가용성 결함) |
| 영향 | DriveECU·SensorECU `CAN1` — Bus-Off 진입 후 자동복구 없음 |
| 관련 | ISO 11898-1(bus-off recovery), AUTOSAR CanSM, hil_runner `setup_bench`, ADR-002 |

---

## D2. 문제 정의 (5W2H · Is–Is Not)

- **무엇:** 양 ECU `MX_CAN1_Init`의 `hcan1.Init.AutoBusOff = DISABLE` → 노드가 Bus-Off에 빠지면 **리셋 전까지 영구히 묶임**(자동 복구 안 함).
- **언제(재현):** `hil_runner --setup`이 st-flash로 **Drive만** 반복 halt/reset(부트로더·베이스라인·메타 쓰는 ~15초). 그동안 살아있는 Sensor가 0x201 등을 송신 → ACK해줄 Drive가 halt → Sensor 송신에러(TEC) 누적 → **Sensor Bus-Off**. 플래시 끝나 Drive 재부팅 → 버스에 혼자 → **Drive도 Bus-Off**.
- **관측:** Drive UART `[ALIVE] … ESR=0xFFFE0047` → BOFF·EPVF·EWGF=1, TEC=254/REC=255, LEC=4(bit recessive error = ACK 없음).
- **Is:** 한 노드만 리셋/플래시. **Is Not:** 두 노드 *동시* 리셋(둘 다 fresh ERROR-ACTIVE로 시작 → 서로 ACK → 정상).
- 배선·종단·비트레이트(500k)는 정상(동시 리셋 시 통신됨으로 배제).

## D4. 근본 원인 분석 (RCA — 5 Whys)

1. 왜 CAN이 죽나? → 한 노드가 Bus-Off 후 회복하지 않는다.
2. 왜 회복 안 하나? → `AutoBusOff(ABOM)=DISABLE`.
3. 왜 Bus-Off에 빠지나? → 플래시 중 파트너(Drive)가 halt → 남은 노드가 ACK 못 받아 TEC saturate.
4. 왜 평소(수동)엔 됐나? → 두 노드를 동시에 리셋하면 둘 다 fresh init → 동시에 버스 진입.
- **근본 원인:** CubeMX 기본값(ABOM 미설정)을 그대로 둠 → **실차 ECU에 필수인 자동 bus-off 복구**가 없음. st-flash가 노드를 비대칭으로 리셋해 이를 노출.

## D5–D6. 영구 시정 조치 & 검증

- 양 ECU `AutoBusOff = DISABLE → ENABLE` (`DriveECU/Core/Src/main.c`·`SensorECU/Core/Src/main.c`).
- **`.ioc`도 동기화**(`CAN1.ABOM=ENABLE` + `CAN1.IPParameters`에 `ABOM` 추가) → CubeMX 재생성 시에도 보존.
- ABOM ON이면 Bus-Off 진입 후 **버스가 idle(128×11 recessive bit) 되면 하드웨어가 자동 재합류** → 한 노드 플래시 후에도 self-healing.
- **검증:** 양 ECU `arm-none-eabi-gcc` 클린 컴파일(0 error/warning). on-target: 한 노드 플래시 후 자동 재합류를 SIT 벤치에서 재확인(후속).

## D7. 재발 방지 (Lessons Learned)

- **CubeMX 기본값은 "데모용"** — 통신 페리페럴은 복구/타임아웃 정책(ABOM, S3 등)을 명시 검토. 표준: ISO 11898-1 bus-off 자동복구, AUTOSAR **CanSM**의 BUS_OFF→복구 상태기계.
- 같은 클래스 점검 대상: `AutoRetransmission=DISABLE`(NART)도 OTA 신뢰성 관점에서 재검토 여지(별도).
- 테스트 운영: 한 노드만 플래시할 땐 다른 노드도 함께 리셋(또는 ABOM으로 self-heal).
