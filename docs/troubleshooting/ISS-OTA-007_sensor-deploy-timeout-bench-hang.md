# ISS-OTA-007: SensorECU OTA 배포 타임아웃 — 벤치 펌웨어 UDS 무응답(메인루프 정지)

> 자동차 실무 **8D** 경량판 (ASPICE SUP.9). 결론: **제품 코드 결함 아님 — 벤치 상태(stale/hang 펌웨어) 이슈**. 파이프라인은 fail-safe로 정상 처리됨(FR-CICD-010 실증).

| 항목 | 내용 |
|---|---|
| 증상 | Jenkins `Deploy SensorECU` 단계 `[OTA] ERROR: Receive timeout` (DiagnosticSessionControl 직후), `OTA_EXIT=1` → 파이프라인 FAILURE |
| 상태 | ✅ 해결됨 (재플래시로 복구, 재실행 PASS) |
| 심각도 | Medium (벤치 상태 이슈 · 무브릭 · fail-safe 동작) |
| 영향 | SensorECU OTA 배포(1대) · CI/CD 배포 단계 · 제품 펌웨어/부트로더 영향 없음 |
| 관련 | FR-CICD-010 · TC-CI-004 · [TR-001 §4.1](../test/TR-001_Test_Report.md) (Jenkins build #85) · [SIT-001 RUNBOOK](../test/SIT-001_RUNBOOK.md) |

---

## D2. 문제 정의 (5W2H · Is–Is Not)

- **무엇이/어디서/언제:** Jenkins 릴리스 배포(build #85, git push v4 트리거)의 `Deploy SensorECU` 단계에서 `ota_client.py`가 첫 UDS 요청(DiagnosticSessionControl Extended, `7E1 02 10 02`) 직후 5초 내 응답을 못 받고 `Receive timeout` → `OTA_EXIT=1` → 파이프라인 FAILURE. **DriveECU 배포는 직전에 정상 완료.**
- **얼마나:** 100% 재현(해당 벤치 상태에서 매 시도). DriveECU 0건/SensorECU 1대.
- **Is** (문제 조건): SensorECU 대상, UDS 요청(`0x7E1`)에 응답 ID `0x7E9` 프레임이 **CAN 덤프에 전혀 없음**. 동시에 SensorECU 앱 데이터 프레임 `0x200`/`0x201`은 **계속 송신**(byte0=APP_VERSION=1, byte1=slot=A).
- **Is Not** (정상 조건): DriveECU(`0x7E0`→`0x7E8`)는 동일 플로우로 SecurityAccess~TransferExit까지 정상. 즉 게이트웨이·CAN 버스·`ota_client`·서명 아티팩트 문제 아님.
- **재현:** 벤치 SensorECU가 해당 상태일 때 `python3 tools/ota_client.py --ecu sensor --channel can0 …` → 즉시 timeout.

## D3. 임시 조치 (Containment)

- 파이프라인이 **이미 fail-safe로 동작** — OTA 실패를 FAILURE로 표시하고 SensorECU를 **이전 펌웨어로 유지**(비활성 슬롯 미commit, 부분 flash·브릭 없음). 추가 봉쇄 불필요. DriveECU는 v4로 정상 갱신됨.

## D4. 근본 원인 분석 (RCA)

- **기법:** Ishikawa 6M + 코드 경로 추적.
- **핵심 단서:** "버스에는 살아있는데(0x200/0x201 송신) UDS만 죽음(0x7E9 무응답)".
  - SensorECU에서 **데이터 송신(TIM3 ISR)·CAN RX 조립(`HAL_CAN_RxFifo0MsgPendingCallback`→`isotp_can_rx`)은 인터럽트**에서 동작 → 0x200/0x201은 계속 나감.
  - 그러나 **UDS 응답 생성 `uds_process()`는 메인 `while(1)` 루프**에서 호출됨([SensorECU/Core/Src/main.c](../../SensorECU/Core/Src/main.c)). 메인 루프가 멈추면 ISR은 살아 데이터는 뿌리지만 UDS 응답은 영영 안 만들어짐 → 클라이언트 timeout.
- **배제한 가설(Is Not 근거):** ① CAN ID 불일치 → Sensor RX `0x7E1`/TX `0x7E9`가 [isotp.h](../../SensorECU/Core/Inc/isotp.h)·클라이언트와 일치(✗). ② 버스/배선 → DriveECU 동일 버스 정상(✗). ③ 서명/버전 → 첫 세션 진입 전 실패라 무관(✗). ④ 클라이언트 버그 → Drive 정상(✗).
- **근본 원인:** **벤치 SensorECU에 올라가 있던 펌웨어의 메인 루프가 정지 상태**(결함주입 빌드 `HIL_SELFTEST_FAIL`의 `while(1){}` 잔류 또는 기타 hang)였음. → 메인 루프 `uds_process()` 미동작 → UDS 무응답. **제품 코드(현재 소스)의 결함이 아니라, 벤치에 stale/주입 빌드가 남아 있던 상태 문제.** (참고: 릴리스로 빌드·서명된 v4 펌웨어는 `-DHIL_SELFTEST_FAIL` 없이 컴파일됨 — 정상.)

## D5–D6. 영구 시정 조치 & 검증

- **시정:** 벤치 SensorECU를 현재 소스(정상 빌드, `HIL_SELFTEST_FAIL` 미정의)로 재플래시 —
  `tools/flash.sh sensor 066EFF485775495067194557 --build` ([SIT-001 RUNBOOK](../test/SIT-001_RUNBOOK.md)) → 메인 루프 정상 기동 → UDS `0x7E9` 응답 복구.
- **검증:** 재실행 **Jenkins build #86 PASS** — SensorECU UDS `02 50 02`(세션)·`06 67 01 …`(seed)→SecurityAccess Unlock→RequestDownload→149블록 100%→RequestTransferExit→`[SensorECU] OTA 완료: SlotB 부팅 확인`. 양 ECU v4 갱신(TR-001 §4.1).

## D7. 재발 방지 (Lessons Learned)

1. **결함주입 빌드 원복 절차:** `HIL_SELFTEST_FAIL` 등 fault-injection 빌드로 TC-02류 시험 후에는 **반드시 정상 빌드로 원복**하는 단계를 SIT-001 RUNBOOK 체크리스트에 명문화(벤치 위생).
2. **배포 전 응답성 사전 점검:** 배포 직전 `read_slot.py`/경량 UDS 핑으로 대상 ECU의 UDS 응답성을 선확인하면 동일 증상을 빌드 시작 전에 차단 가능(후속 개선 후보).
3. **파이프라인 견고성 확인(긍정):** 이 사건은 **OTA 실패 → 파이프라인 FAILURE → 이전 펌웨어 유지**(FR-CICD-010)를 실제로 실증 — 한 ECU 장애가 다른 ECU(Drive 성공)를 오염시키지 않고, 실패가 브릭이 아니라 무변경으로 수렴함을 확인. UN R156/ISO 24089의 "배포 실패 시 안전상태 유지" 원칙과 정합.

---

## 교훈 한 줄

> "ECU가 데이터는 뿌리는데 진단에 무응답"이면 — **ISR은 살고 메인 루프가 죽은 것**. UDS 응답은 메인 루프(`uds_process`) 책임이므로, 0x200/0x201 생존 ≠ UDS 생존.
