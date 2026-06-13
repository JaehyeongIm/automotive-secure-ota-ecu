# RTM-001: 요구사항 추적성 매트릭스 (Requirements Traceability Matrix)

| 항목 | 내용 |
|---|---|
| 문서 ID | RTM-001 |
| 버전 | 1.0 |
| 작성일 | 2026-06-07 |
| 목적 | SRS-001(v2.13) 전체 요구를 **설계(SDD-001/ADR) ↔ 코드 ↔ 테스트 ↔ 상태**로 양방향 추적(ASPICE SWE.6/SYS.5, ISO 26262-6) |
| 참조 | SRS-001(v2.13), SDD-001(v1.2), TR-001(v1.5), SIT-001, TEST_SPEC v1.0 |

> **범례:** ✅ 구현 + 검증(단위/on-target) · 🔶 구현(앱·on-target·코드리뷰만, 전용 단위 없음/부분) · ⬜ 미구현/이연 · 🔁 대체설계로 충족
> 상태는 **2026-06-07** 기준. ⬜/🔶 사유는 SRS 인라인 "구현 현황"·§19.1 잔여위험 레지스터·관련 ISS/ADR 참조.

---

## 요약 (상태 분포)

| 영역 | ✅ | 🔶 | ⬜/🔁 | 비고 |
|---|---|---|---|---|
| 보안 핵심(FR-AB·SR-FW·SR-ATK·SR-KEY) | 다수 | 일부 | replay·campaign·fake-complete | on-target 8/8 PASS |
| UDS/CAN(FR-CAN) | 009~013·016·019 | 011(hw_id) | **014(0x31)·015(0x11)·017·018** | 미구현은 대체설계 or Should |
| Manifest(SR-MF·SR-UP) | 002·004·006 | — | **001🔁·007·008·UP-003/005** | 서명 헤더로 대체(§8.1) |
| 부트로더/Flash(FR-BL·FR-FL) | 대부분 | 004/005·009 | — | OTA 수신은 앱(아래) |
| 앱/게이트웨이/CI(FR-DRV·SEN·GW·CICD) | staleness 등 | 대부분(on-target/수동) | GW-008·CICD-006🔁 | 단위 범위 밖 |
| NFR | REL 전부·SAFE·MNT | PERF-003 | MNT-004(Should) | REL-003=S3 구현 |

**핵심 미구현/이연(⬜):** FR-CAN-014(0x31 Verify)·015(0x11 Reset)·017(replay nonce)·018(RDBID) · SR-MF-007(campaign)·008(freshness) · SR-UP-003/005 · SR-ATK-005(replay)·010(campaign) · FR-GW-008 — 대부분 **Should** 또는 **대체설계/HW(ATECC608A)·Uptane-lite 로드맵**(§19.1).

---

## 1. Gateway (FR-GW) — `tools/ota_client.py`, RPi5

| ID | 요구(요약) | 설계 | 코드 | 검증 | 상태 |
|---|---|---|---|---|---|
| FR-GW-001 | OTA Gateway 역할 | SDD §2.4 | ota_client.py, RPi5 | on-target(SIT-TC 전체) | 🔶 |
| FR-GW-002 | Update Package/Manifest 파싱 | §8.1 | ota_client.py | — | 🔁 Manifest 미사용, 서명 이미지 전송(§8.1) |
| FR-GW-003 | Manifest 필드 관리 | §8.1 | ota_client.py | — | 🔁 ecu_id/ver=헤더, sig=ECDSA |
| FR-GW-004 | ECU별 업데이트 순서 제어 | — | ota_client.py(--ecu) | on-target | 🔶 |
| FR-GW-005 | CAN UDS 송수신 | §6.6 | ota_client.py(isotp) | on-target(unlock~exit) | ✅ |
| FR-GW-006 | 성공/실패 로그 | — | ota_client.py stdout | on-target | 🔶 |
| FR-GW-007 | 공격자 시나리오 모드(Should) | §SIT-TC-05~07 | forge_image·--declared-size | SIT-TC-05/06/07 | ✅ |
| FR-GW-008 | Campaign ID 관리(Should) | — | — | — | ⬜ Uptane-lite 후속 |

## 2. Bootloader (FR-BL) — `Bootloader/Core/Src/bootloader.c`, `ota_meta`

| ID | 요구(요약) | 설계 | 코드 | 검증 | 상태 |
|---|---|---|---|---|---|
| FR-BL-001 | Custom Bootloader 포함 | §2.3 | bootloader.c | on-target 부팅 | ✅ |
| FR-BL-002 | 활성/후보 슬롯 구분 | §4 | ota_meta_select | test_meta(7) | ✅ |
| FR-BL-003 | 유효 App jump | §6.1 | jump_to_app | on-target(`Jump 0x…200`) | ✅ |
| FR-BL-004 | CAN 업데이트 세션 진입 | §6.6 | — | — | 🔶 **OTA 수신은 앱(uds.c)**, BL은 검증·부팅(설계 divergence) |
| FR-BL-005 | 펌웨어를 비활성 슬롯 기록 | §6.6 | uds.c·ota_flash(앱) | on-target | 🔶 앱에서 기록(BL 아님) |
| FR-BL-006 | 펌웨어 Hash 검증 | §6.2 | bootloader.c sha256 | SIT-TC-06 | ✅ ECDSA에 내포 |
| FR-BL-007 | Signature 검증 | §6.2 | uECC_verify | SIT-TC-06/07 | ✅ |
| FR-BL-008 | 다운그레이드 차단 | §4.3,6.3 | version_allowed | test_anti_rollback·SIT-TC-01 | ✅ |
| FR-BL-009 | Target ECU ID/HW ID 확인 | §6.6 | ecu_id_allowed(앱 0x37) | test_anti_rollback·SIT-TC-07 | 🔶 ecu_id✅, **hw_id 이연(§19.1 L-2)**, BL-level은 후속 |
| FR-BL-010 | 첫 부팅 Self-test 확인 | §5.3,5.4 | plan_boot·self_confirm | test_meta_lifecycle·SIT-TC-02 | ✅ |
| FR-BL-011 | 메타 손상 안전정책(Should) | §6.2 | verify_decision·CRC | test_bootloader_slot·test_meta | ✅ |
| FR-BL-012 | IWDG + 5s 세션 abort | §2.5 | IWDG·uds_process(S3) | test_uds_state(S3 2)·SIT-TC-02/08 | ✅ |
| FR-BL-013 | Bootloader WRP 보호(Should) | §14.2 | 링커·옵션바이트 nWRP0/1 | on-target: 양 보드 nWRP0/1=active + 섹터0 erase 거부([WRP-PROV-001](../test/WRP-PROV-001_Bootloader_WRP_Provisioning.md)) | ✅ |

## 3. A/B Slot & Rollback (FR-AB) — `ota_meta`, `ota_flash`, `bootloader.c`

| ID | 요구(요약) | 설계 | 코드 | 검증 | 상태 |
|---|---|---|---|---|---|
| FR-AB-001 | A/B 5상태 슬롯 | §4 | ota_meta(slot status) | test_meta·meta_lifecycle | ✅ |
| FR-AB-002 | 비활성 슬롯만 기록(UPDATING) | §6.6 | uds.c·ota_flash | on-target | ✅ |
| FR-AB-003 | 정적검증 전 부팅대상 불가(fail-closed) | §6.2 | bootloader_verify_decision | test_bootloader_slot(15)·SIT-TC-03 | ✅ |
| FR-AB-004 | Self-test PASS→CONFIRMED(ECU 자신) | §5.3 | ota_meta_self_confirm | test_ota_meta(10)·SIT-TC-01 | ✅ |
| FR-AB-005 | 메타 원자적 갱신(전원차단 내성) | §4.1,6.4 | meta ping-pong·CRC·seq | test_meta(7) | ✅ |
| FR-AB-006 | 메타 필드 관리(Should) | §13.3 | OTA_Metadata_t | test_meta | ✅ |
| FR-AB-007 | 3-strike 롤백 | §5.4,6.1 | plan_boot | test_meta_lifecycle(8)·SIT-TC-02 | ✅ |
| FR-AB-008 | 서명 보호 Image Header | §4.3 | OTA_ImgHeader_t·sign_firmware | test_anti_rollback·SIT-TC-06 | 🔶 **헤더 4필드 subset**(7필드 스펙 대비, §8.1) |

## 4. CAN / ISO-TP / UDS (FR-CAN) — `uds.c`, `isotp.c`

| ID | 요구(요약) | 설계 | 코드 | 검증 | 상태 |
|---|---|---|---|---|---|
| FR-CAN-001~008 | CAN·ISO-TP(SF/FF/CF/FC/STmin)·UDS 절차 | §6.6 | isotp.c·uds.c | test_uds_state·on-target OTA | ✅ |
| FR-CAN-009 | DiagnosticSessionControl(0x10) | §6.6 | uds.c case 0x10 | test_uds_state | ✅ |
| FR-CAN-010 | SecurityAccess(0x27) HMAC | §6.5 | uds.c·hmac_sha256 | test_uds_state(29)·test_hmac(3)·on-target | ✅ |
| FR-CAN-011 | RequestDownload(0x34)+ecu_id/hw_id | §6.6 | uds.c 0x34·ecu_id_allowed | test_anti_rollback·ADR-009·SIT-TC-07 | 🔶 ecu_id✅(0x37), **hw_id 이연** |
| FR-CAN-012 | TransferData(0x36)+누적상한 | §6.6 | uds.c 0x36 cap | test_uds_state(2)·SIT-TC-05 | ✅ ISS-SEC-001 |
| FR-CAN-013 | RequestTransferExit(0x37)+완료검증 | §6.6 | uds.c 0x37 | test_uds_state(2) | ✅ |
| FR-CAN-014 | RoutineControl-Verify(0x31) | §6.2 | — | (부팅 검증으로 대체) | ⬜ **0x31 미구현** — 부팅 fail-closed 대체(SR §FR-CAN-014 주석) |
| FR-CAN-015 | ECU Reset(0x11) | — | (자동리셋 대체) | — | ⬜ **0x11 미구현** — 0x37 후 자동리셋 대체 |
| FR-CAN-016 | NRC 정의 | §13.5 | uds.c nrc() | test_uds_state(거부분기) | ✅ |
| FR-CAN-017 | Replay nonce(session_id)(Should) | — | — | — | ⬜ freshness HW 의존(§19.1 L-1) |
| FR-CAN-018 | ReadDataByIdentifier(0x22)(Should) | — | — | — | ⬜ 미구현 |
| FR-CAN-019 | S3 세션 타임아웃(5s) | §2.5,6.6 | uds.c uds_process | test_uds_state(2)·SIT-TC-08 | ✅ |

## 5. App — Drive(FR-DRV)/Sensor(FR-SEN)

| ID | 요구(요약) | 코드 | 검증 | 상태 |
|---|---|---|---|---|
| FR-DRV-001 | TB6612 모터 PWM | DriveECU/main.c·drive.c | on-target | 🔶 |
| FR-DRV-002 | B1 EXTI 주행 시작 | DriveECU/main.c | on-target(SIT-TC-04) | 🔶 |
| FR-DRV-003 | 장애물 10cm 정지 | drive.c | on-target | 🔶 |
| FR-DRV-004 | v2 자동 후진(Should) | drive.c | on-target | 🔶 |
| FR-DRV-006 | App version/slot CAN 보고 | drive.c(0x100) | on-target heartbeat | ✅ |
| FR-DRV-007 | OTA 중 주행 유지 | main.c 루프 | on-target | 🔶 |
| FR-DRV-008 | TransferExit(0x37) 시 즉시 재부팅 활성화 | DriveECU/uds.c(0x37 NVIC_SystemReset) | on-target(SIT-TC-01~) | ✅ |
| FR-SEN-001 | HC-SR04 거리 읽기 | SensorECU/main.c | on-target | 🔶 |
| FR-SEN-002 | 임계 10cm | SensorECU/main.c | on-target | 🔶 |
| FR-SEN-003 | 100ms heartbeat(Should) | SensorECU/main.c(0x201) | on-target | 🔶 |
| FR-SEN-004 | 거리값 0x200 송신 | SensorECU/main.c | on-target(SIT-TC-04) | ✅ |
| FR-SEN-005 | version/slot 보고 | SensorECU/main.c | on-target | ✅ |
| ISO 26262 staleness | 센서 침묵→fail-safe | drive_sensor_fresh·drive.c | gcc(6)·SIT-TC-04 | ✅ |

## 6. CI/CD (FR-CICD) — `Jenkinsfile`, `ci/build.sh`, `sign_firmware.py`

| ID | 요구(요약) | 코드 | 검증 | 상태 |
|---|---|---|---|---|
| FR-CICD-001 | Jenkins on RPi5 | Jenkinsfile | on-target(build #85/#86, TR §4.1) | ✅ |
| FR-CICD-002 | push 트리거 | Jenkinsfile | on-target(build #85, TR §4.1) | ✅ |
| FR-CICD-003 | arm-gcc 크로스컴파일 | ci/build.sh | CI 로그 | ✅ |
| FR-CICD-004 | cppcheck 정적분석(Should) | Jenkinsfile | CI 로그 | 🔶 |
| FR-CICD-005 | 바이너리 크기 검사 | Jenkinsfile | CI 로그 | ✅ |
| FR-CICD-006 | ECDSA 서명 + Manifest 생성 | sign_firmware.py | 서명 검증 | 🔁 **Manifest 미생성**, 서명 이미지만(§8.1) |
| FR-CICD-007 | IDLE 확인 후 다운로드·TransferExit 즉시 활성화 | ota_client.py(wait_for_idle)·uds.c(0x37) | on-target | ✅ |
| FR-CICD-008 | Stage 로그 기록 | Jenkinsfile | CI 이력 | ✅ |
| FR-CICD-009 | 개인키 Credentials | Jenkinsfile | 코드리뷰 | ✅ |
| FR-CICD-010 | 배포 실패 FAILURE 표시 | Jenkinsfile | on-target(build #85, TR §4.1) | ✅ |

## 7. Flash Partition (FR-FL)

| ID | 요구(요약) | 코드 | 검증 | 상태 |
|---|---|---|---|---|
| FR-FL-001 | 링커 BL/App 주소 분리 | *.ld | objdump·on-target | ✅ |
| FR-FL-002 | App Vector Table 주소 검증 | bootloader.c is_valid_app | test_bootloader_slot·on-target | ✅ |
| FR-FL-003 | Flash erase/write 슬롯 경계 내 | uds.c(slot_max+cap)·ota_flash | test_uds_state(size)·SIT-TC-05 | ✅ |
| FR-FL-004 | App 크기 초과 실패처리 | ci(FR-CICD-005)·uds 0x34 | CI·test_uds_state | ✅ |
| FR-FL-005 | 메타 손상검출(CRC)(Should) | ota_meta crc32 | test_meta | ✅ |

## 8. 보안 — Manifest(SR-MF)/Firmware(SR-FW)/Key(SR-KEY)/Update(SR-UP)

| ID | 요구(요약) | 코드/설계 | 검증 | 상태 |
|---|---|---|---|---|
| SR-MF-001 | 패키지에 Manifest 포함 | §8.1 서명 헤더 | — | 🔁 헤더 대체 |
| SR-MF-002 | target_ecu_id | 헤더·ecu_id_allowed | test_anti_rollback·SIT-TC-07 | ✅ |
| SR-MF-003 | firmware_version | 헤더·version_allowed | test_anti_rollback·SIT-TC-01 | ✅ |
| SR-MF-004 | image_size | 메타·0x34 size | test_uds_state·SIT-TC-05 | ✅ |
| SR-MF-005 | image_hash | ECDSA SHA-256 흡수 | SIT-TC-06 | ✅ |
| SR-MF-006 | signature | 이미지 ECDSA-P256 | SIT-TC-06/07 | ✅ |
| SR-MF-007 | campaign_id(Should) | — | — | ⬜ Uptane-lite |
| SR-MF-008 | expiration/freshness(Should) | — | — | ⬜ §19.1 L-1 |
| SR-FW-001 | 펌웨어 Hash 계산 | bootloader sha256 | SIT-TC-06 | ✅ |
| SR-FW-002 | Signature 검증 | uECC_verify | SIT-TC-06/07 | ✅ |
| SR-FW-003 | 낮은 version 거부 | version_allowed | test_anti_rollback·SIT-TC-01 | ✅ |
| SR-FW-004 | 타 ECU ID 이미지 거부 | ecu_id_allowed | test_anti_rollback·ADR-009 | ✅ |
| SR-FW-005 | Slot 초과 크기 거부 | uds 0x34 slot_max | test_uds_state(2) | ✅ |
| SR-FW-006 | Flash write 주소 범위 검증 | uds 0x36 cap·slot 경계 | test_uds_state·SIT-TC-05 | ✅ |
| SR-KEY-001 | 공개키 BL Flash 하드코딩 | bootloader ecdsa_pubkey | 코드리뷰 | ✅ |
| SR-KEY-002 | 개인키 ECU 미저장 | sign_firmware/Jenkins | 코드리뷰 | ✅ |
| SR-KEY-003 | PSK BL Flash 저장 | psk.c @0x08007FE0 | 칩 read·test_hmac·on-target | ✅ |
| SR-KEY-004 | 키 갱신=BL 재플래시(Should) | WRP·psk.c | 코드리뷰 | ✅ |
| SR-UP-001 | ECU Inventory 수집 | — | — | ⬜ RDBID(0x22) 미구현 연계 |
| SR-UP-002 | ECU별 대상 이미지 구분 | 헤더 target_ecu_id | SIT-TC-07 | ✅ |
| SR-UP-003 | Campaign 결과 관리(Should) | — | — | ⬜ Uptane-lite |
| SR-UP-004 | ECU 자체 검증 | bootloader ECDSA(GW 불신) | SIT-TC-06/07 | ✅ Uptane 핵심 |
| SR-UP-005 | Manifest 재사용 방어(Should) | — | — | ⬜ freshness 후속 |

## 9. 공격 시나리오 (SR-ATK ↔ TC-ATK/SIT-TC) — TARA 연계

| ID | 위협 | 방어 | 검증 | 상태 |
|---|---|---|---|---|
| SR-ATK-001 | Firmware Tampering | Hash/Sig | SIT-TC-06(변조→ECDSA FAILED) | ✅ |
| SR-ATK-002 | Arbitrary(unsigned) | Sig | SIT-TC-07(미서명→ECDSA FAILED) | ✅ |
| SR-ATK-003 | Downgrade | Anti-rollback | SIT-TC-01·test_anti_rollback | ✅ |
| SR-ATK-004 | ECU Mismatch | target_ecu_id | test_anti_rollback·ADR-009 | ✅(단위) |
| SR-ATK-005 | Replay | session_id/freshness | — | ⬜ HW 의존(§19.1 L-1·ADR-004/006) |
| SR-ATK-006 | Unauthorized/Brute Force | SecurityAccess+잠금 | test_uds_state(잠금) | ✅(단위) |
| SR-ATK-007 | Endless Data | size/cap | SIT-TC-05·test_uds_state(2) | ✅ ISS-SEC-001 |
| SR-ATK-008 | CAN Flood/DoS | timeout/abort/rollback | SIT-TC-08(no-brick)·S3 | ✅ |
| SR-ATK-009 | Fake Complete | ECU 내부 검증 commit | SIT-TC-03(fail-closed)·verify_decision | 🔶 메커니즘 확인 |
| SR-ATK-010 | Partial Bundle | campaign 추적 | — | ⬜ Uptane-lite |

## 10. 비기능 (NFR)

| ID | 요구(요약) | 충족 근거 | 상태 |
|---|---|---|---|
| NFR-REL-001 | 실패 시 정상 App 유지 | anti-rollback·3-strike(SIT-TC-01/02) | ✅ |
| NFR-REL-002 | 전원차단 복구 | 메타 원자성 CRC(test_meta) | ✅ |
| NFR-REL-003 | 통신중단 시 무한대기 X | **S3 타임아웃(FR-CAN-019, uds_process)** | ✅ |
| NFR-REL-004 | Self-test 실패 rollback | 3-strike(SIT-TC-02) | ✅ |
| NFR-PERF-001 | Chunk 단위 안정 전송 | ISO-TP(on-target 35KB) | ✅ |
| NFR-PERF-002 | 업데이트 시간 측정(Should) | SDD §2.5 타이밍·~20s | ✅ |
| NFR-PERF-003 | drive_update ≤10ms(Should) | main 루프 | 🔶 미측정 |
| NFR-MNT-001 | 메시지 명세 문서화 | SRS §13·SDD §2.4 | ✅ |
| NFR-MNT-002 | 상태 전이 다이어그램 | diagram.md | ✅ |
| NFR-MNT-003 | 테스트/결과 추적 | TR-001·**본 RTM** | ✅ |
| NFR-MNT-004 | v1/v2 릴리즈 노트(Should) | FR-DRV·SRS changelog 산재 | 🔶 전용 문서 없음 |
| NFR-SAFE-001 | Erase 구간 모터 정지 | main.c(erase 중 g_ota_active) | 🔶 |
| NFR-SAFE-002 | 장애물 시 감속/정지(Should) | drive.c | 🔶 |
| NFR-SAFE-003 | BL 상태 모터 X | bootloader(모터 미구동) | ✅ |
| NFR-SAFE-004 | Safe State 정의 | bootloader safe_state·SIT-TC-03 | ✅ |

---

## 개정 이력
| 버전 | 날짜 | 내용 |
|---|---|---|
| 1.0 | 2026-06-07 | 최초 — SRS-001(v2.13) 전체 113개 요구를 설계·코드·테스트·상태로 단일 RTM화. 분산돼 있던 SDD §8·TR §6·SRS 인라인 추적을 통합, ⬜/🔶/🔁 정직 표기(미구현·대체설계·HW/Uptane-lite 이연 명시) |
