# HIL-001: HIL(실보드) 테스트 플랜 — Secure OTA 보안·안전 기능

| 항목 | 내용 |
|---|---|
| 문서 ID | HIL-001 |
| 레벨 | 시스템/통합 테스트 (ASPICE SWE.6, ISO 26262-6) |
| 대상 | DriveECU + SensorECU + RPi5 게이트웨이 + CAN 버스 (on-target) |
| 작성일 | 2026-06-04 |
| 추적 | FR-BL-008, FR-AB-003/004/007, FR-AB-008, SR-FW-003, ISO 26262 안전상태 |

> 호스트 단위테스트(61개)는 *순수 로직*("입력 X→반환 Y")을 증명한다. 이 플랜은 *실보드에서
> 실제로 그렇게 동작하는가*(플래시 쓰기·부팅·점프·CAN·타이밍)를 검증한다. 둘은 상호보완이며,
> 단위테스트가 못 잡는 통합/하드웨어 결함을 여기서 잡는다.

---

## 1. 테스트 환경

### 1.1 장비
| 구성 | 용도 |
|---|---|
| STM32F446RE ×2 (DriveECU, SensorECU) | 피시험 ECU(SUT) |
| ST-Link v2/v3 | 펌웨어 플래시 + **메타데이터 위조 주입**(fail-closed 자극) |
| CAN 트랜시버 + 버스, RPi5 게이트웨이 | OTA 전송(UDS/ISO-TP) + 버스 관측 |
| ECU 디버그 UART ×2 (115200 8N1) | `[BL]`/`[DRIVE]`/`[SensorECU]` 로그 관측 |
| 벤치 전원, Sensor 전원/CAN 분리 스위치 | staleness 자극 |

### 1.2 관측 채널(Observable) — "테스트 포인트"
| 신호 | 출처 | 무엇을 알 수 있나 |
|---|---|---|
| **CAN 0x100** data[0]=ver, data[1]=slot | Drive heartbeat | *실제 부팅된 Drive 펌웨어의 버전·활성 슬롯* |
| **CAN 0x201** data[0]=ver, data[1]=slot | Sensor heartbeat | 실제 부팅된 Sensor 버전·슬롯 |
| **CAN 0x200** | Sensor 장애물 프레임 | Sensor 생존/침묵 (staleness 자극원) |
| **UART `[BL] …`** | 부트로더 | 검증 결정·전이·거부 사유 |
| **UART `[DRIVE]`/`[SensorECU]`** | 앱 | 앱 진입·상태·fail-safe |
| LD2 토글 | 양 ECU | heartbeat 생존(LED) |

### 1.3 메모리 맵(자극 시 참조)
```
0x08000000 부트로더(sector 0–1)   0x08008000 Meta A   0x0800C000 Meta B
0x08010000 Slot A(이미지 base; 앱 벡터 +0x200)   0x08040000 Slot B
```

### 1.4 핵심 관측 로그(정확한 문자열)
```
[BL] ECDSA OK
[BL] version vN OK
[BL] anti-rollback: vN below baseline — refusing
[BL] metadata size invalid (0x........) — fail-closed, refusing
[BL] meta transition seq X->Y
[BL] Safe State: all slots invalid. Reflash via ST-Link.
[BL] Jump to 0x........  SP=...  PC=...
[SensorECU vN] Start, Slot=N        [DRIVE vN] 출발
[DRIVE] 센서 stale → fail-safe 정지
```

---

## 2. 필요 픽스처 (별도 작업: HIL 항목 2에서 제작)
| ID | 픽스처 | 제작 방법 |
|---|---|---|
| F1 | 서명 정상 이미지 v1/v2/v3 | `sign_firmware.py <bin> <key> --version N --ecu-id E` |
| F2 | **고장 앱**(self-test 실패) | heartbeat 송신 전에 hang하는 빌드(IWDG가 리셋 → confirm 안 됨). 서명 |
| F3 | **size=0 위조 메타** | `OTA_Metadata_t`(magic 유효, slotX CONFIRMED, slotX_size=0, seq↑, **CRC 재계산**) 바이너리 → ST-Link로 0x08008000 주입. 헬퍼 `tools/forge_meta.py` 신규 |

---

## 3. 테스트 케이스

### HIL-TC-00 — 베이스라인: 정상 OTA + self-test commit (sanity)
- **목적/추적:** FR-AB-004 — 정상 업데이트가 부팅·자가확정(TRIAL→CONFIRMED)되는지(이후 TC의 기준)
- **전제:** Slot A=CONFIRMED v1 실행 중(heartbeat 0x100: ver=1, slot=0)
- **절차:** ① 게이트웨이로 v2 서명이미지 OTA 푸시 → ② ECU 자동 리셋
- **기대(관측):**
  - 부트로더: `meta transition`(UPDATED→TRIAL) → `ECDSA OK` → `version v2 OK` → `Jump 0x08010200`(또는 B)
  - 앱: `[… v2] Start/출발`, **첫 heartbeat 후 self-confirm**
  - 두 번째 리셋(전원 재인가) 후에도 v2가 CONFIRMED로 유지(롤백 안 됨)
- **합격:** heartbeat 0x100 ver=2 + 재부팅해도 v2 유지(= self-test commit 성공)

### HIL-TC-01 — anti-rollback: 서명된 옛 버전 거부 (FR-BL-008/FR-AB-008)
- **목적:** 정상 서명됐지만 *더 낮은* 버전을 부트로더가 거부
- **전제:** Slot=CONFIRMED **v2** 실행(기준선=2). 픽스처 F1의 서명 **v1** 준비
- **절차:** ① v1 서명이미지를 비활성 슬롯으로 OTA 푸시 → ② 리셋
- **기대(관측):**
  - 부트로더: `ECDSA OK`(v1 서명은 진짜) → **`anti-rollback: v1 below baseline — refusing`** → `Safe State`
  - heartbeat: **v1이 한 번도 안 나타남**(v2 유지 또는 정지)
- **합격:** v1 미부팅 + 거부 로그. (⚠ 현재 거부 후 **safe_state(halt)** — §5 권고 참조)
- **음성 대조 HIL-TC-01b:** 같은 슬롯에 v3(상위) 푸시 → `version v3 OK` → 부팅. (상위는 허용)

### HIL-TC-02 — 3-strike 롤백: 고장 업데이트 자동 복구 (FR-AB-007)
- **목적:** self-test 실패 업데이트가 3회 시도 후 이전 CONFIRMED로 롤백
- **전제:** Slot A=CONFIRMED **v2**(정상). 픽스처 F2 **고장 v3**(서명 OK, self-test 실패)
- **절차:** ① 고장 v3를 Slot B로 OTA 푸시(→UPDATED, active=B) → ② 자동 리셋, 이후 개입 없이 관측
- **기대(관측 — 부팅 사이클):**
  | 부팅 | 로그 | 결과 |
  |---|---|---|
  | 1 | `meta transition`(UPDATED→TRIAL, count=1) → Jump B | 앱 hang → IWDG 리셋(~8s) |
  | 2 | `meta transition`(count→2) → Jump B | hang → 리셋 |
  | 3 | `meta transition`(count→3) → Jump B | hang → 리셋 |
  | 4 | count+1>3 → B를 INVALID 마킹 + **Slot A 롤백** | v2 부팅 |
- **기대 최종:** heartbeat 0x100 **ver=2, slot=0**(롤백 완료), 고장 v3 더 이상 시도 안 됨
- **합격:** ≤4 사이클 내 v2/SlotA 자동 복구. (⚠ 3-strike 전용 로그 부재 — §5 권고)

### HIL-TC-03 — fail-closed: size=0 서명검증 우회 차단 (FR-AB-003)
- **목적:** 메타 size가 비정상이면 ECDSA를 *건너뛰지 않고* 부팅 거부
- **전제:** Slot A=CONFIRMED v2 실행
- **절차:** ① ST-Link로 픽스처 F3(slot_a_size=0, CRC 유효, seq↑) 메타를 0x08008000 주입 → ② 리셋
- **기대(관측):** 부트로더 **`metadata size invalid (0x00000000) — fail-closed, refusing`** → `Safe State`. **앱 미부팅(heartbeat 없음)**
- **합격:** size=0인데도 부팅되지 않음 + fail-closed 로그
- **음성 대조 HIL-TC-03b:** 정상 size 메타로 복구(재-OTA 또는 정상 메타 주입) → `ECDSA OK` → 정상 부팅(정상 경로 회귀 확인)

### HIL-TC-04 — 센서 staleness fail-safe (ISO 26262 안전상태)
- **목적:** Sensor 침묵 시 Drive가 타임아웃 내 정지
- **전제:** 양 ECU 정상, Sensor 0x200 송신 중(장애물 없음), Drive 출발(B1) → 전진 중(모터 ON, 0x100 driving=1)
- **절차:** ① Drive 주행 확인 → ② **SensorECU 전원/CAN 분리**(0x200 중단)
- **기대(관측):** 마지막 0x200 후 **~150ms 내** Drive `[DRIVE] 센서 stale → fail-safe 정지`, 모터 정지(0x100 driving=0)
- **합격:** 센서 침묵 후 ≤~150ms 내 정지 + stale 로그
- **음성 대조 HIL-TC-04b(시동 stale):** Sensor OFF 상태로 Drive 전원 인가 → B1 눌러도 **출발 안 함**(첫 0x200 전 = stale). Sensor ON 후 B1 → 정상 출발

---

## 4. 추적 매트릭스
| TC | 요구사항 | 단위테스트(로직) | HIL(실보드) |
|---|---|---|---|
| TC-00 | FR-AB-004 | test_ota_meta(self_confirm) | 본 플랜 |
| TC-01 | FR-BL-008/FR-AB-008 | test_anti_rollback(version_allowed) | 본 플랜 |
| TC-02 | FR-AB-007 | test_meta_lifecycle(plan_boot 3-strike) | 본 플랜 |
| TC-03 | FR-AB-003 | test_bootloader_slot(verify_decision) | 본 플랜 |
| TC-04 | ISO 26262 안전상태 | gcc(drive_sensor_fresh) | 본 플랜 |

---

## 5. 관측성 개선 권고 (플랜 작성 중 발견)
1. **3-strike 전용 로그 부재** — 현재 롤백은 `meta transition`(seq만)과 최종 heartbeat로만 관측됨. 권고: 임계 초과 시 `[BL] 3-strike: slot X INVALID, rollback to slot Y` 로그 추가 → TC-02 관측 명확화.
2. **anti-rollback 거부 후 halt** — `ota_meta_version_allowed` 실패 시 `safe_state()`로 정지(재플래시 필요). 권고 검토: 이전 CONFIRMED 슬롯으로 **graceful fallback** 후 부팅(가용성↑) — 보안(옛 버전 미실행)은 유지하면서. TC-01 기대결과는 결정에 따라 갱신.
3. **fail-closed 자극엔 메타 위조 도구 필요** — `tools/forge_meta.py`(CRC 포함) 제작 전제(픽스처 F3).

---

## 6. 실행 기록 템플릿
| TC | 일시 | 펌웨어/슬롯 | 관측 로그·heartbeat | 결과(P/F) | 비고 |
|---|---|---|---|---|---|
| TC-00 | | | | | |
| TC-01 | | | | | |
| TC-01b | | | | | |
| TC-02 | | | | | |
| TC-03 | | | | | |
| TC-03b | | | | | |
| TC-04 | | | | | |
| TC-04b | | | | | |

**합격 판정:** 모든 TC(음성 대조 포함) PASS 시 해당 기능 실보드 검증 완료. FAIL 시 8D 트러블슈팅(ISS) 발행.
