# ISS-SEC-001: OTA TransferData 누적 수신 상한·완료 검증 누락 (endless-data)

> 트러블슈팅(8D 경량판). 코드 리뷰(공격 시나리오 커버리지 점검) 중 발견한 보안 결함.

| 항목 | 내용 |
|---|---|
| 증상 | 작은 image_size를 선언한 뒤 TransferData(0x36)를 계속 보내면 선언 크기를 넘겨 거부 없이 인접 flash까지 기록됨 |
| 상태 | ✅ 해결됨 (코드 수정 + 단위 2종) · on-target(SIT-TC-05) 이연 |
| 심각도 | High |
| 영향 | DriveECU·SensorECU `uds.c` OTA 다운로드 경로(0x36/0x37) — 슬롯 경계 초과 기록 시 인접 슬롯/메타 손상 가능 |
| 관련 | FR-CAN-012, FR-CAN-013, SR-ATK-007, TC-ATK-007 ↔ SIT-TC-05, [TR-001 §5](../TR-001_Test_Report.md), ADR-010 |

---

## D2. 문제 정의 (5W2H · Is–Is Not)

- **무엇:** `0x36 TransferData` 핸들러가 `g_fw_written += chunk_len` 누적만 하고 **`g_fw_written + chunk_len > g_fw_size` 상한 검사가 없음.** `0x37 RequestTransferExit`도 **`g_fw_written == g_fw_size` 완료 검증이 없음.**
- **어디서:** `DriveECU/Core/Src/uds.c`, `SensorECU/Core/Src/uds.c`.
- **얼마나:** 공격자가 RequestDownload(0x34)에서 작은 size(예: 4B)를 선언하고 256B 블록을 반복 전송 → `ota_flash_write(g_fw_addr + g_fw_written, …)`가 슬롯 경계를 넘어 **다음 슬롯/부트 메타까지 기록.** 세션 종료도 없어 무한 지속(endless-data, SR-ATK-007).
- **Is:** 누적 전송 > image_size, 또는 불완전(미달) 전송. **Is Not:** 정확히 image_size만큼 보내는 정상 OTA(정상 동작 유지 — 회귀 없음).
- **재현:** 단위 `test_transfer_data_exceeds_declared_size_returns_nrc_and_aborts`(수정 전이면 0x31 미발생). on-target은 SIT-TC-05.

## D4. 근본 원인 분석 (RCA — 5 Whys)

1. 왜 초과 기록되나? → 0x36에 누적 상한 검사가 없다.
2. 왜 없었나? → 크기 검증을 0x34(RequestDownload)의 *선언값*(size==0/초과)만으로 끝내고, 전송 *루프*의 누적 검증(FR-CAN-012 Must)을 구현하지 않았다.
3. 왜 발견이 늦었나? → SRS/TR 문서는 "FR-CAN-012 누적상한 구현"으로 표기했으나 실제 코드에 반영되지 않았다(문서-코드 불일치).
4. 왜 불일치가 통과됐나? → Must 요구 ↔ 코드 위치의 대조 검증 없이 "구현"으로 표기(과대표기).
- **근본 원인:** FR-CAN-012/013(Must)이 코드에 미구현 + 문서 표기와 코드의 추적 대조 부재.

## D5–D6. 영구 시정 조치 & 검증

**시정(두 ECU 동일):**
- `0x36`: 쓰기 전 `if (g_fw_written + chunk_len > g_fw_size) { g_state = STATE_DEFAULT; nrc(sid, 0x31); break; }` — 거부 + **세션 종료**(DriveECU는 `g_ota_active=0` 병행). FR-CAN-012.
- `0x37`: 헤더 deref 전 `if (g_fw_written != g_fw_size) { nrc(sid, 0x24); break; }`. FR-CAN-013.

**검증:**
- 단위 2종 신규(SensorECU 빌드): 초과→NRC 0x31+세션종료(이후 0x36은 0x22), 불완전→NRC 0x24. `ceedling test:all` **80/80 PASS**(기존 78 + 2). 정상 size 전송 경로 회귀 없음.
- DriveECU 미러는 동일 패치 + 코드리뷰(단위 빌드 미포함 — project.yml :source가 SensorECU/Bootloader), **on-target SIT-TC-05**로 실증 예정.

## D7. 재발 방지 (Lessons Learned)

- **문서-코드 추적 강화:** TR-001의 요구사항 커버리지를 "구현 주장"이 아니라 *코드 위치(파일:함수) + 테스트 ID*로 검증. 본 건으로 §5의 007 표기 정정(🔶 과대표기 → ✅ 단위, on-target 이연 명시).
- **결함 클래스:** "길이를 누적하는 핸들러는 상한 검사 + 완료 검증을 쌍으로 둔다"를 리뷰 체크리스트에 추가(다른 누적 경로 점검).
- 표준: ISO 14229(UDS TransferData/RequestTransferExit), SR-ATK-007(endless-data), CWE-770(Allocation of Resources Without Limits).
