# SDD-001: Secure OTA ECU — 소프트웨어 설계서 (Software Design Description)

| 항목 | 내용 |
|---|---|
| 문서 ID | SDD-001 |
| 범위 | Bootloader + DriveECU/SensorECU App + 공유 모듈 + 게이트웨이 도구 |
| 레벨 | SW 아키텍처 설계(ASPICE SWE.2) + SW 상세 설계(SWE.3), IEEE 1016 |
| 작성일 | 2026-06-04 |
| 참조 | SRS-001(v2.13), ADR-002~010, SIT-001, HARA-001, TARA-001 |
| 표준 근거 | ISO 24089(SW update), AUTOSAR(UCM/NvM/Fee/Csm 패턴), ISO 14229(UDS)·15765(ISO-TP), ISO 26262-6, ISO/SAE 21434 |

> 본 문서는 *무엇을(SRS)* → *어떻게(설계)* → *어디에(코드)* → *증명(테스트)* 의 추적을 기술한다.
> 모든 설계 요소는 §8 추적성 매트릭스에서 요구사항·코드·검증과 연결된다.
>
> **문서 구성 — SAD/SDD 통합본.** §2 = SW 아키텍처(ASPICE **SWE.2**: 컴포넌트·인터페이스·
> 리소스/타이밍·동적행위·대안평가), §3~6 = SW 상세 설계(**SWE.3**: 모듈·데이터·알고리즘).
> 프로젝트 규모상 SAD와 SDD를 하나의 문서로 통합하되 레벨을 절로 분리한다(IEEE 1016 SDD는
> 아키텍처~상세를 포괄).

---

## 1. 개요

### 1.1 목적
SRS-001의 기능·보안·안전 요구사항을 실현한 소프트웨어 아키텍처와 상세 설계를 기술한다.

### 1.2 설계 원칙 (전체를 관통하는 4가지)
1. **SSOT/DRY** — 메타데이터·상태머신·암호 규약 등 *진실의 단일 출처*를 `ota_meta`에 두고 부트로더·양 ECU가 동일 정의를 공유(드리프트 방지).
2. **테스트가능성 우선** — HAL 의존 코드와 *순수 로직*을 분리. 순수 함수는 호스트(Ceedling/gcc)에서 단위 검증, HAL 통합은 on-target(SIT-001)에서 검증.
3. **Fail-closed / Deny-by-default** — 검증 불가·비정상 입력은 *허용이 아니라 거부*(CWE-636 failing-open 방지, ISO 24089).
4. **Defense-in-depth** — 부트로더 WRP 잠금 → ECDSA 서명 → anti-rollback → 메타 CRC 원자성 → SecurityAccess의 다층 방어.

---

## 2. 시스템 컨텍스트 & 아키텍처 (SWE.2)

### 2.1 컨텍스트
```
 [개발 PC]──git push──>[RPi5 게이트웨이]
                         │ Jenkins: build→sign→OTA
                         │ ota_client.py (UDS/ISO-TP)
                         ▼ CAN 버스 (500 kbit/s)
        ┌────────────────┴────────────────┐
   [DriveECU STM32F446]            [SensorECU STM32F446]
   0x100 hb / 0x7E0·8 UDS          0x200 obs·0x201 hb / 0x7E1·9 UDS
        └──── 0x200 장애물 프레임 ────┘
```

### 2.2 플래시 메모리 맵 (ECU 1개, STM32F446RE 512KB)
```
0x08000000  Bootloader        (sector 0–1, 32KB)   ← WRP 잠금, PSK @0x08007FE0
0x08008000  Metadata A        (sector 2, 16KB)     ┐ 이중화(ping-pong)
0x0800C000  Metadata B        (sector 3, 16KB)     ┘
0x08010000  Slot A            (sector 4–5, 192KB)  app 벡터 @ +0x200 (앞 헤더)
0x08040000  Slot B            (sector 6–7, 256KB)  app 벡터 @ +0x200
```

### 2.3 레이어 & 컴포넌트
```
┌─ Application ───────────────────────────────────────────┐
│  main(제어루프) · drive.c(주행FSM·staleness) · uds.c(OTA서버)│
├─ Service / Middleware ──────────────────────────────────┤
│  isotp.c(ISO-TP) · ota_flash.c(슬롯·메타 쓰기) · ota_psk   │
├─ Security primitives ───────────────────────────────────┤
│  sha256 · hmac_sha256 · uECC(ECDSA P-256)                │
├─ Pure core (HAL 무관, 호스트 테스트) ─ ★ SSOT ───────────┤
│  ota_meta.c : CRC·select·plan_boot·plan_confirm·         │
│               img_header_read·version_allowed            │
├─ HAL (ST STM32F4xx) ────────────────────────────────────┤
└─────────────────────────────────────────────────────────┘
Bootloader = bootloader.c + (ota_meta.c·sha256·uECC·psk) — 앱과 ota_meta 공유
```

### 2.4 컴포넌트 인터페이스 (provided / required)

**내부 (모듈 API):**
| 컴포넌트 | Provides | Requires |
|---|---|---|
| `ota_meta`(순수) | crc32 · meta_crc/valid/select · plan_boot · plan_confirm · img_header_read · version_allowed | — |
| `ota_flash` | erase_slot_a/b · flash_write · meta_write_pending · self_confirm · get_active_slot | HAL_FLASH · ota_meta |
| `bootloader` | run · select_boot_addr · verify_decision | ota_meta · sha256 · uECC · psk · HAL |
| `uds` | init · process · on_isotp_rx | isotp_send · ota_flash · hmac_sha256 · ota_psk |
| `isotp` | init · send · can_rx(RX 콜백) | HAL_CAN |
| `drive` | init · update · note_sensor_rx · sensor_fresh | motor · HAL · g_obstacle/g_button(공유 변수) |

**외부 (CAN / UDS):**
| 인터페이스 | 방향 | 내용 |
|---|---|---|
| 0x100 / 0x201 | ECU → | heartbeat: data[0]=fw_version, data[1]=active_slot |
| 0x200 | Sensor → Drive | 장애물: data[0]=flag, data[1:2]=거리(cm) |
| 0x7E0↔0x7E8 / 0x7E1↔0x7E9 | GW ↔ ECU | UDS over ISO-TP (Drive / Sensor) |
| UDS SID(구현) | GW → ECU | 0x10 세션 · 0x27 SecurityAccess · 0x34 RequestDownload · 0x36 TransferData · 0x37 TransferExit (5종). **미구현:** 0x22 RDBID(FR-CAN-018)·0x31 Verify(FR-CAN-014, 부팅 fail-closed로 대체)·0x11 Reset(FR-CAN-015, 자동리셋 대체)·0x3E TesterPresent |

### 2.5 리소스 & 타이밍 예산

**Flash (512KB):**
| 영역 | 할당 | 사용(실측) | 비고 |
|---|---|---|---|
| Bootloader | 32KB (s0–1) | <32KB (uECC·sha256 포함) | WRP 잠금, PSK @0x08007FE0 |
| Metadata ×2 | 16KB×2 (s2–3) | 44B/사본 | 섹터 단위 이중화 |
| Slot A | 192KB (s4–5) | ~35KB 서명이미지 | ~157KB 여유 |
| Slot B | 256KB (s6–7) | ~35KB | ~221KB 여유 |

**RAM:** 128KB SRAM (`_estack = 0x20020000`). 앱 정적+스택 modest, 여유 충분.

**타이밍:**
| 항목 | 값 | 출처 |
|---|---|---|
| CAN 비트레이트 | 500 kbit/s | 설정 |
| IWDG 윈도우 | ~8000ms (prescaler 256·reload 999, LSI 32kHz) | 부트로더 arm |
| Tboot (리셋~self-test 완료) | 측정값, IWDG 윈도우 미만 | ADR-002 |
| 3-strike 총지연 | ≤ 3 × Tboot_fail ≈ ~30s (SIT TC-02 실측) | FR-AB-007 |
| 센서 staleness 타임아웃 | 150ms (센서 주기 50ms × 3, E-Gas) | §6.7 |
| S3 UDS 세션 타임아웃 | 5000ms | FR-CAN-019 |
| SecurityAccess 잠금 | 3회 → 10s | ADR-003 |
| OTA 처리량 | ~35KB / ~20s (≈1.8 KB/s, ISO-TP CF-delay 5ms) | on-target 실측 |

---

## 3. 정적 설계 — 모듈 (SWE.3)

| 모듈 | 책임 | HAL 의존 | 위치 | 검증 |
|---|---|---|---|---|
| `bootloader.c` | 부팅 결정·ECDSA·anti-rollback·메타 커밋·jump | 예(#ifndef UNIT_TEST 분리) | Bootloader/Core/Src | test_bootloader_slot(15) + on-target |
| **`ota_meta.{c,h}`** | **SSOT**: 메타구조·CRC·select·plan_boot/confirm·img header·version_allowed·ecu_id_allowed | **아니오(순수)** | 3곳 복제(Sensor/Drive/BL) | test_meta·ota_meta·meta_lifecycle·anti_rollback(35) |
| `ota_flash.c` | 슬롯 erase/write·메타 ping-pong 쓰기·self_confirm | 예 | {Drive,Sensor}/Core/Src | test_ota_meta(RAM 하니스) |
| `uds.c` | UDS 서버: SecurityAccess·RequestDownload·TransferData/Exit | 일부 | {Drive,Sensor}/Core/Src | test_uds_state(29) |
| `isotp.c` | ISO-TP(15765) 세그먼테이션·흐름제어 | 예 | 〃 | mock 기반 |
| `hmac_sha256`·`sha256` | SecurityAccess·ECDSA 해시 | 아니오 | 〃 | test_hmac(RFC4231) |
| `uECC` | ECDSA-P256 verify | 아니오 | Bootloader/Core/Lib | on-target(ECDSA OK) |
| `drive.c` | 주행 FSM + 센서 freshness fail-safe | 일부 | DriveECU/Core/Src | gcc(drive_sensor_fresh) + on-target |
| `psk.c`/`ota_psk` | PSK 저장(부트로더 WRP)·접근 | 예 | 〃 | — |

**SSOT 메커니즘:** `ota_meta.{c,h}`는 물리적으로 3곳에 *동일 사본*으로 존재하나 정의는 단일. 빌드는 각 프로젝트가 자기 사본을 컴파일하되, 변경 시 3곳 동기화(드리프트 발생 시 메타 레이아웃 불일치 → 즉시 빌드/CRC 실패로 검출). → ADR 없음, 설계 규칙.

---

## 4. 데이터 설계

### 4.1 Boot Metadata (`OTA_Metadata_t`, 11×uint32 = 44B)
| 필드 | 의미 |
|---|---|
| magic (0xDEADBEEF) | 유효성 |
| seq_counter | monotonic — 이중화 사본 중 최신 식별 |
| active_slot | 0=A,1=B |
| slot_a/b_status | 슬롯 5상태(§4.2) |
| slot_a/b_version | 헤더 fw_version 기록(anti-rollback 기준선) |
| boot_count | TRIAL 부팅 시도수(3-strike) |
| slot_a/b_size | 서명 이미지 크기(ECDSA 영역) |
| crc32 (마지막 워드) | 앞 10워드 CRC-32(ISO-HDLC) |

- **이중화(FR-AB-005)**: 섹터 2·3 두 사본. 읽기 = CRC 유효 & seq 최대(`ota_meta_select`). 쓰기 = *비활성 사본*에 본문 후 CRC 마지막 = **ping-pong 원자적 commit**(전원차단 torn-write 안전, AUTOSAR Fee/NvM redundant 패턴).

### 4.2 슬롯 생명주기 (5-state, §7.3.1)
```
 INVALID(CCCC) ──OTA──> UPDATING(DDDD) ──기록완료──> UPDATED(BBBB)
                                                        │ 부트로더 trial
                                                        ▼
        CONFIRMED(AAAA) <──self-test PASS── TRIAL(EEEE)
              ▲                                  │ 3-strike(>3)
              └──────────── rollback ────────────┴──> INVALID
```

### 4.3 서명 이미지 헤더 (`OTA_ImgHeader_t`, 앞 헤더 / ADR-007)
```
서명 이미지 = [ 헤더 0x200 ][ 벡터테이블+코드 ][ ECDSA 서명 64B ]
  magic(0x4F544148='OTAH') · fw_version · target_ecu_id · reserved
  └ ECDSA가 (헤더+코드)를 덮음 → fw_version 위조 불가. 앱 벡터=slot+0x200.
```

### 4.4 부팅 계획 (`OTA_BootPlan_t` + `OTA_BootEvent_t`)
`{write, boot_slot, event}` — event ∈ {CONFIRMED, TRIAL_START, TRIAL_RETRY, ROLLBACK, FALLBACK, FACTORY, SAFE}. 부트로더가 event별 진단 로그 출력(on-target 관측성).

---

## 5. 동적 설계 — 핵심 시퀀스

### 5.1 부팅 (bootloader_run)
```
select(metaA,metaB) → plan_boot(meta,max=3)
  ├ UPDATED→TRIAL(count=1)·write   ├ TRIAL→retry(count++)   ├ TRIAL>3→INVALID+rollback
  └ CONFIRMED→boot                 └ 메타없음→factory A
→ [write면 bl_meta_commit(ping-pong)]
→ boot_slot<0면 safe_state
→ verify_decision(meta,size,slot_max):
     메타없음→SKIP · size정상→REQUIRED · size비정상(0/초과/0xFFFFFFFF)→REFUSE(fail-closed)
→ REQUIRED: SHA256(헤더+코드) → uECC_verify → 실패 safe_state
            → img_header_read → version_allowed(헤더 ver ≥ CONFIRMED 기준선)?
                 아니면 거부슬롯 INVALID + 이전 CONFIRMED로 rollback+reset
→ IWDG 시작 → jump_to_app(slot + 0x200)   (VTOR=slot+0x200)
```

### 5.2 OTA (gateway ↔ uds.c)
```
0x10 ExtendedSession → 0x27 SecurityAccess(Seed→HMAC(PSK,Seed)[:4] key)
 → 0x34 RequestDownload(비활성 슬롯 target) → 0x36 TransferData×N(ISO-TP)
 → 0x37 TransferExit: 헤더 파싱→fw_version → ota_meta_write_pending(slot,size,ver)
        (slot=UPDATED, active=slot, seq+1, ping-pong) → NVIC_SystemReset
```

### 5.3 Self-test commit (FR-AB-004)
앱: main 도달 + 페리페럴 init + **첫 heartbeat 송신 성공**(ISR 플래그) → 메인루프에서 `ota_meta_self_confirm(active)` → TRIAL→CONFIRMED·count=0. (플래시 쓰기는 ISR 아닌 메인루프.)

### 5.4 3-strike 롤백 (FR-AB-007) — §5.1 plan_boot의 TRIAL 분기. 점프-직전-증가 → hang/crash/reset 모든 실패가 카운트 → >3에서 INVALID+이전 CONFIRMED.

### 5.5 센서 staleness fail-safe (ISO 26262)
0x200 수신마다 `drive_note_sensor_rx`(시각 기록). `drive_update` 진입 시 `drive_sensor_fresh(seen,now,last,150ms)` == stale면 `drive_force_stop` + IDLE. (미수신/age>150ms = 장애물로 간주, AUTOSAR E2E 타임아웃.)

---

## 6. 상세 설계 — 보안·안전 핵심

| # | 설계요소 | 핵심 알고리즘 | 근거/한계 |
|---|---|---|---|
| 6.1 | **부팅 결정** `ota_meta_plan_boot` | 5상태 전이 순수함수 + event | FR-AB-007 |
| 6.2 | **검증 게이팅** `bootloader_verify_decision` | 메타有+size비정상→REFUSE | FR-AB-003, CWE-636 |
| 6.3 | **anti-rollback** `img_header_read`+`version_allowed` | 서명헤더 ver vs CONFIRMED 최고 ver | FR-BL-008, ADR-007. ⚠기준선=메타(CRC)→물리공격 한계→ATECC608A |
| 6.4 | **메타 원자성** `ota_meta_select`+ping-pong | seq+CRC, 비활성 사본 후 CRC 마지막 | FR-AB-005, AUTOSAR NvM |
| 6.5 | **SecurityAccess** | key=HMAC-SHA256(PSK,Seed)[:4], 3회→잠금 | FR-CAN-010, RFC2104. ⚠seed=SW nonce(ADR-004)·잠금=RAM(ADR-003) |
| 6.6 | **OTA 프로토콜·ECU 식별** uds.c+isotp.c | UDS 0x10/27/34/36/37 over ISO-TP; 0x37에서 헤더 target_ecu_id≠자기ID→NRC 0x31 | ISO 14229/15765, FR-CAN-011, ADR-009. ⚠UDS 우회(직접플래시)는 부트로더 후속 |
| 6.7 | **센서 freshness** `drive_sensor_fresh` | unsigned 뺄셈(wrap 안전), seen+timeout | ISO 26262 안전상태 |

---

## 7. 설계 결정 (ADR)
| ADR | 결정 | 상태 |
|---|---|---|
| 002 | Tboot은 *측정*(임의값 아님) | accepted |
| 003 | SecurityAccess 잠금 = RAM(리셋 시 해제) → NV는 ATECC608A 후속 | 잠정 |
| 004 | Seed = SW nonce(F446 무TRNG) → TRNG는 ATECC608A 후속 | 잠정 |
| 005 | ADR=MADR, ISS=8D, 파일명 규칙 | accepted |
| 006 | ATECC608A(secure element) 도입 — TRNG·monotonic counter·PSK 슬롯 | accepted(HW 주문) |
| 007 | anti-rollback: 앞 서명헤더 + 메타(CRC) 기준선 | accepted(잠정 기준선) |
| 009 | ECU 식별: 앱 컴파일타임 ID로 타 ECU 이미지 거부(0x37 NRC 0x31) | accepted(부트로더 후속) |

---

## 8. 추적성 매트릭스 (요구사항 → 설계 → 코드 → 검증)

> 본 §8은 **보안·안전 핵심**의 추적이며, 전체 113개 요구의 양방향 추적은 [RTM-001](../requirements/RTM-001_Requirements_Traceability_Matrix.md) 참조.

| SRS 요구 | 설계 요소(§) | 코드 | 단위테스트 | On-target |
|---|---|---|---|---|
| FR-AB-005 메타 원자성 | §4.1, 6.4 | ota_meta_select·meta_write_copy | test_meta(7) | (TC 공통) |
| FR-AB-007 3-strike | §5.4, 6.1 | ota_meta_plan_boot | test_meta_lifecycle(8) | **SIT TC-02 PASS** |
| FR-AB-004 self-test commit | §5.3 | ota_meta_self_confirm·main.c | test_ota_meta(10) | SIT TC-01 경유 |
| FR-AB-003 fail-closed | §6.2 | bootloader_verify_decision | test_bootloader_slot(15) | **SIT TC-03 PASS** |
| FR-BL-008/AB-008 anti-rollback | §4.3, 6.3 | img_header_read·version_allowed | test_anti_rollback(10) | **SIT TC-01 PASS** |
| FR-CAN-011 ECU 식별 | §4.3, 6.6 | ecu_id_allowed·uds.c(0x37 NRC 0x31) | test_anti_rollback(10) | (코드리뷰·ADR-009) |
| FR-CAN-010 SecurityAccess | §6.5 | uds.c·hmac_sha256 | test_uds_state(29)·test_hmac(3) | SIT TC-01/02 unlock |
| ISO 26262 센서 staleness | §5.5, 6.7 | drive_sensor_fresh·drive.c | gcc(6 케이스) | **SIT TC-04 PASS** |
| FR-CAN-012/013 endless-data | §6.6 | uds.c(0x36 누적상한→NRC 0x31·0x37 완료검증→0x24) | test_uds_state(2) | **SIT TC-05 PASS** (ISS-SEC-001) |
| FR-CAN-019/NFR-REL-003 S3 타임아웃 | §2.5·6.6 | uds.c `uds_process`(5s 무요청→세션 abort) | test_uds_state(2) | SIT TC-08 (ISS-OTA-006) |
| TC-ATK-001/002 변조·미서명 거부 | §6.2 | bootloader.c `uECC_verify`·`tools/forge_image.py` | test_bootloader_slot(verify_decision) | **SIT TC-06/07 PASS** |
| CAN Bus-Off 자동복구(ABOM) | §2.5 | MX_CAN1_Init AutoBusOff=ENABLE | — | (ISS-CAN-006) |

---

## 9. 검증 요약
- **호스트 단위테스트 91개**(Ceedling, 라인 91%·분기 79% 커버리지 `ceedling gcov:all`) + gcc 독립검증(drive_sensor_fresh 6) — *순수 로직*.
- **On-target 벤치 9종(SIT-001)** — 기본 4(fail-closed·anti-rollback·3-strike·staleness, 2026-06-04) + 공격 5(endless-data·변조·미서명·CAN flood no-brick, 2026-06-07; SecurityAccess reboot-replay, 2026-06-13), 전부 PASS.
- 추적: §8로 *요구사항↔설계↔코드↔테스트* 양방향 연결.

## 10. 개정 이력
| 버전 | 날짜 | 내용 |
|---|---|---|
| 1.0 | 2026-06-04 | 최초 — 구현·단위테스트·on-target 검증 완료 시점 기준 아키텍처(SWE.2)+상세설계(SWE.3) 기술, 추적성 매트릭스 포함 |
| 1.1 | 2026-06-05 | ECU 식별 강제(FR-CAN-011) 구현 반영 — §6.6에 0x37 target_ecu_id 검사(NRC 0x31) 추가, 순수함수 `ota_meta_ecu_id_allowed`(§8 추적 행 신설), ADR-009 채택. 단위테스트 78개(라인 91%·분기 79%) |
| 1.2 | 2026-06-07 | 세션 구현 반영 — endless-data(FR-CAN-012/013)·S3(FR-CAN-019)·ABOM(ISS-CAN-006)·공격 4종 on-target(SIT-TC-05~08). §8 추적행 4개 추가, §2.4 SID표에 미구현(0x22/0x31/0x11/0x3E) 표기, §9 on-target 8종, 단위 82(test_uds_state 29). 참조 SRS v2.13·ADR-010 동기화 |
| 1.3 | 2026-06-17 | §9 검증 요약 단위테스트 82→91 정합 — SecurityAccess seed freshness(SR-ATK-005, `test_sec_freshness` 5)·`test_ota_meta` 10→13 반영(상세 [TR-001](../test/TR-001_Test_Report.md) v1.9) |
