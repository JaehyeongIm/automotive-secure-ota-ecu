# FT-001: 퍼즈 테스트 계획·결과 (Fuzz Test Plan & Report)

| 항목 | 내용 |
|---|---|
| 문서 ID | FT-001 |
| 문서명 | Secure OTA 호스트 퍼즈 테스트 계획·결과 |
| 레벨 | 사이버보안 검증 — 퍼즈 테스트 (ASPICE for Cybersecurity **SEC.3** Risk Treatment Verification) |
| 상태 | **진행 중** (P1 착수, 2026-06-07) |
| 작성일 | 2026-06-07 |
| 대상 | 호스트 컴파일 **순수 파서**: `ota_meta`(헤더·메타) + `uds` 디스패처 (HAL 분리 코어) |
| 참조 산출물 | [SRS-001](../requirements/SRS-001_CAN_Secure_OTA_Pipeline.md)(SR-ATK·SR-FW·FR-CAN-012/013), [TARA-001](../security/TARA-001_Threat_Analysis_Risk_Assessment.md)(T-1·T-3·T-6·T-8), [TR-001](TR-001_Test_Report.md), [RTM-001](../requirements/RTM-001_Requirements_Traceability_Matrix.md) |
| 표준 근거 | **ISO/SAE 21434 §10.4(RC-10-12)** — 퍼즈를 사이버보안 검증 방법으로 권고 / **ASPICE-CS SEC.3** — 검증전략·테스트명세·결과 / **ISO/IEC/IEEE 29119-3** — 테스트 문서 템플릿 |

> 본 문서는 *호스트(소스레벨) 커버리지 가이드 퍼징*의 **계획**과 **결과**를 함께 기록한다.
> 실차 위에서의 프로토콜 퍼징(on-target)은 범위 밖이며 [SIT-001](SIT-001_System_Integration_Test_Plan.md) 계열로 다룬다(§2).
> 미실행·이연 항목은 단정하지 않고 [SRS §19.1] 잔여 위험으로 연결한다.

---

## 1. 목적 · 범위

**목적.** OTA가 신뢰하지 않는 입력(공격자 통제 가능 바이트)을 파싱하는 순수 로직을, **커버리지 가이드 퍼징 + 새니타이저**로 자동·대량 자극해 메모리 안전 결함을 체계적으로 발굴·차단한다. 발견은 [TARA-001]의 위협(T-1 변조·T-8 endless-data 등)에 대한 *검증 채널*로 연결된다.

| 구분 | 범위 |
|---|---|
| **In** | 호스트 컴파일되는 순수 파서: `ota_img_header_read`, `ota_meta_select`/`ota_meta_valid`, UDS 디스패처(`uds_on_isotp_rx`→`uds_process`→`handle`) |
| **Out** | 실차 CAN 위 프로토콜/블랙박스 퍼징(→ on-target, 별도 단계) · HAL/페리페럴 · 부트로더 점프 |

## 2. 검증 전략 (SEC.3)

**계층 구분.** 퍼징은 대상의 가시성에 따라 둘로 나뉜다. 본 문서는 **호스트(소스레벨)** 만 다룬다.

| 계층 | 대상 | 도구 | 본 문서 |
|---|---|---|:--:|
| **호스트(소스레벨)** | 소스가 있는 순수 파서, 메모리 버그 | **libFuzzer / AFL++ + ASAN/UBSAN** | ✅ |
| 온타깃(프로토콜) | 실차 CAN 위 UDS/ISO-TP 블랙박스 | scapy-automotive · caringcaribou 등 | ⬜ 후속 |

**도구 선정 근거.**
- **커버리지 가이드 퍼징**(libFuzzer/AFL++): 코드 커버리지를 신호로 입력을 진화 → ISO/SAE 21434가 높은 CAL에 권고하는 *adaptive* 퍼징에 해당. OSS-Fuzz·Chromium·Linux 커널이 쓰는 사실상 표준.
- **ASAN/UBSAN**: 평시 조용한 경계 위반(OOB)·정의되지 않은 동작(정수 오버플로)을 *그 순간* 크래시로 드러내는 산업 표준 새니타이저.
- **하니스 인터페이스 `LLVMFuzzerTestOneInput`**: libFuzzer·AFL++·Honggfuzz·OSS-Fuzz가 공통으로 받는 ABI → 엔진 lock-in 없음(컴파일러만 교체하면 AFL++ 전환).
- **빌드**: Homebrew LLVM(`fuzz/build.sh`). Apple clang은 libFuzzer 미포함 → Homebrew `llvm` 사용. `-fsanitize=fuzzer,address,undefined -DUNIT_TEST`.
- **자산 분리 근거**: 기능 단위테스트(Ceedling)가 이미 HAL과 순수 코어를 분리(`project.yml :source`)해 둠 → 동일 코어를 그대로 퍼징 하니스에 재사용.

## 3. 퍼즈 타깃 명세 (Test Design / Case — 29119-3)

각 하니스 = 한 공격 표면. **oracle**(무엇을 결함으로 보는가)과 **중단 기준**을 명시한다.
타깃 순서는 **리스크 기반**(ISO/SAE 21434)으로 정한다 — ①노출(입력이 공격자에 직접 통제되나) ②복잡도(수동 메모리·길이 연산) ③결과(연결 TARA 위협 Risk)의 곱이 큰 것부터.

| 우선 | 하니스 | 진입점 | 입력 모델 | Oracle | 매핑 요구·위협 | 상태 |
|:--:|---|---|---|---|---|:--:|
| **P1 (최상)** | **FH-3** `harness_uds` | `uds_on_isotp_rx`→`uds_process` | 상태 프라이밍 후 0x36/0x37 스트림 | ASAN/UBSAN + 플래시모델 경계 + `g_fw_written≤g_fw_size` 불변식 | FR-CAN-012/013·SR-ATK-007 / **T-8**(Risk 3) | ✅ **F-003 수정 · v2/SC3 입증** |
| P2 (중하) | **FH-2** `harness_meta` | `ota_meta_select`/`ota_meta_valid` | 메타 2섹터(struct×2) | ASAN/UBSAN + CRC/seq 선택 불변식 | FR-AB-005 (전원차단 견고성) | ⬜ 후속 |
| P3 (낮음) | **FH-1** `harness_imghdr` | `ota_img_header_read` | 이미지 선두 헤더 바이트(≥16B) | ASAN/UBSAN + magic 분기 | SR-FW-002·FR-AB-008·FR-CAN-011 / T-1·T-3·T-6 | ✅ 완료 |

> **우선순위 근거.** FH-3는 ①0x36 와이어 바이트를 *직접* 통제 ②상태+길이계산+flash 쓰기로 복잡 ③실제 버그가 살았던 endless-data(CWE-770, [ISS-SEC-001](../troubleshooting/ISS-SEC-001_endless-data-no-cumulative-cap.md))·T-8 → 세 기준 모두 1위라 **다음 타깃**. FH-1은 ②복잡도·①노출이 낮아(파싱 메모리안전만) **리그 스모크테스트로 선행 후 완료**. FH-2(메타)는 공격자가 와이어로 직접 못 넣는 내부 값이라 보안 노출이 낮아 **후순위**(견고성 커버리지).

**시드/사전:** FH-3 = 정상 0x34/0x36/0x37 흐름 · FH-2 = 정상 메타 1쌍 · FH-1 = 정상 서명 헤더(`diag_slotb_signed.bin`).
**공통 oracle:** ① 새니타이저 abort(OOB·overflow) ② libFuzzer 행/타임아웃(무한루프) ③ 하니스 불변식 assert.
**커버리지 목표:** 대상 파서의 도달 가능한 분기를 시드+퍼징으로 모두 실행(`-print_coverage` 확인).
**중단(완료) 기준:** 하니스별 무신규커버리지 지속 또는 크래시 발견 시(크래시는 §4로). — FH-1은 이 기준 충족으로 완료(§6).
**축소 모델 주(FH-3 v2):** endless-data 누적 퍼징은 퍼저가 슬롯 경계에 도달하도록 플래시 슬롯을 **16KB로 축소 모델링**(실 256KB의 1/16 — 메커니즘 동일, 크기만 스케일). 입력을 *길이바이트 증폭*으로 다중블록화해 누적·seq 진행·누적가드를 자극한다.

## 4. 트리아지 · 분류 절차

> **핵심 원칙: 모든 크래시가 취약점은 아니다.** 발견 → 아래 절차로 *진짜 결함*과 *하니스/계약 산물*을 구분한다.

1. **재현** — 크래시 입력으로 단발 재생(`bin/<harness> <crash-file>`), 스택·접근유형 확보.
2. **분류** — 다음 중 하나로:
   - **(A) 실제 결함** — 운영 경로에서 공격자가 트리거 가능 → CWE·심각도 부여, §5 처리.
   - **(B) 계약/하니스 산물** — 운영 호출부에서는 발생 불가(예: 실호출부가 항상 충분한 버퍼 전달). → 하니스에서 전제(precondition) 반영, 필요 시 *방어적 보강 후보*로만 기록.
3. **중복 제거** — 동일 root-cause는 1건으로 묶음(libFuzzer dedup + 수동 확인).
4. **기록** — §7 Findings에 등재. (A)는 [troubleshooting] 8D(ISS-SEC-*) 발행.

## 5. 결함 처리 루프

발견(A) → **순수 로직에 수정** → **Ceedling 회귀 케이스 추가**(`ceedling test:all` 그린) → 동일 하니스로 **재퍼징해 미재현 확인** → 잔여는 [SRS §19.1]·[TARA-001]로 연결.

## 6. 캠페인 로그 · 결과 (Test Log — 29119-3)

| 일시 | 하니스 | execs | 커버리지 | 크래시 | 고유결함 | 비고 |
|---|---|---|---|---|---|---|
| 2026-06-07 | FH-1 `harness_imghdr` (가드 전) | (1차) | magic 분기 | 1 | 0(A) / 1(B) | F-001 발견 → (B) 분류 후 하니스 가드 |
| 2026-06-07 | FH-1 `harness_imghdr` (가드 후) | 29.8M / 11s (2.7M/s) | cov 6 정체 | 0 | 0 | **완료** — 운영결함 0. libFuzzer CMP가 magic("HATO")을 자동발견해 valid 분기까지 커버, 이후 무신규 → 중단기준 충족 |
| 2026-06-08 | FH-3 `harness_uds` sanity | 프라이밍 1회 | — | 1(UBSAN) | 1(A) | 프라이밍(HMAC) 검증 중 **UBSAN이 sha256.c UB 검출 → F-002**. 프라이밍 자체는 0x76까지 정상 |
| 2026-06-08 | FH-3 `harness_uds` v1(단일블록) | <1초 | cov 241+ | 1 | 1(A) | **F-003 발견** — 261B 블록 → `padded[260]` 스택오버플로(uds.c:252, CWE-121) |
| 2026-06-08 | FH-3 `harness_uds` (수정 후 재퍼징) | 1.4M / 21s (67k/s) | — | 0 | 0 | per-block 가드(chunk_len>256→NRC 0x31) 후 **무크래시** → 방어 입증. 회귀 test_uds_state 30/30 |
| 2026-06-08 | FH-3 v2(다중블록, 16KB 슬롯모델) | 1.23M / 26s (47k/s) | cov 253 | 0 | 0 | endless-data 누적 경로 — 가드 있음 → **무크래시**(누적·per-block 가드가 슬롯 내 유지). 방어 성립 |
| 2026-06-08 | FH-3 v2 **SC3 리그 입증** | <초 | — | 1(의도) | — | 누적가드 일시 제거 → **플래시모델이 endless-data OOB 검출**(ASAN heap-overflow @ 16384B 경계, `ota_flash_write`). 가드 복원 후 재퍼징 무크래시 → 리그·FR-CAN-012 가드 유효 입증 |

## 7. 발견 사항 (Findings)

### F-001 — `ota_img_header_read` 헤더 크기 미만 입력 시 경계 초과 읽기
- **하니스/입력:** FH-1, 빈 입력(0바이트).
- **관측:** `AddressSanitizer: heap-buffer-overflow / READ` @ `ota_meta.c:131` (`memcpy(&h, image_start, sizeof(h))` — 길이 검사 없이 16바이트 고정 읽기).
- **분류: (B) 계약/하니스 산물.** 운영 호출부 `uds.c`의 `ota_img_header_read((const uint8_t*)g_fw_addr, …)`는 **플래시 슬롯 포인터**를 넘기므로 항상 헤더 크기 이상 존재 → 실기기 미발생.
- **분류 근거 CWE:** CWE-125(Out-of-bounds Read) *클래스*에 해당하나 현 설계에서 **비악용(non-exploitable)**.
- **조치:** ① 하니스에 전제 가드(`size < sizeof(OTA_ImgHeader_t)` 스킵) 반영 → 실파싱 로직만 퍼징. ② **방어적 보강 후보(낮음):** `ota_img_header_read`에 길이 파라미터/경계검사 추가(미래 호출부의 짧은 버퍼 대비). RTM 후속 항목으로만 등재, 즉시 변경 안 함(현 호출부 안전).

### F-002 — `sha256.c` 부호 있는 좌측 시프트 오버플로 (UB)
- **하니스/입력:** FH-3 sanity — 프라이밍의 HMAC-SHA256 경로(SecurityAccess seed `0xA5A5A5A5`).
- **관측:** `UBSan: left shift of 165 by 24 … type 'int'` @ `sha256.c:49` — `(data[j] << 24)`에서 `0xA5`(uint8_t→int 승격)<<24 가 int 범위 초과 → **부호 있는 시프트 오버플로(UB, CWE-190 계열)**.
- **분류: (A) 실제 코드 결함.** 운영 SHA-256(펌웨어 해싱·HMAC)도 `0x80`↑ 바이트마다 발생 → 실기기에서도 일어남(타깃에선 의도대로 동작하나 UB/MISRA 위반). **심각도 낮음.** 범위: sha256.c **3개 사본**(Sensor·Bootloader·Drive) 동일.
- **조치:** 시프트 전 `(WORD)` 캐스트 → 부호 없는(정의된) 시프트. 3개 사본 모두 수정.
- **회귀:** UBSAN 클린(sanity 재실행) · `test_hmac`(RFC 4231) **3/3** · `test_uds_state` **29/29** PASS → 값 불변·UB 제거 입증. → [ISS-SEC-003](../troubleshooting/ISS-SEC-003_sha256-signed-shift-ub.md) 8D.
- **의의:** 정상 단위테스트가 통과하던 코드에서 **sanitizer(UBSAN)가 잠복 UB를 표면화** — FT-001 oracle이 실제로 작동함을 입증.

### F-003 — UDS TransferData(0x36) per-block 스택 버퍼 오버플로 (CWE-121)
- **하니스/입력:** FH-3 `harness_uds` v1(단일 블록), **261바이트** 입력으로 **<1초** 발견.
- **관측:** `AddressSanitizer: stack-buffer-overflow` @ `uds.c:252` (`memset(padded,0xFF,write_len)`). chunk_len=261 → write_len=264 > `padded[260]` → 인접 스택 침범(이어지는 `memcpy`는 공격자 바이트로 침범).
- **분류: (A) 실제 취약점.** post-auth(unlock 필요)지만 **공격자 제어 스택 오버플로** → 베어메탈 Cortex-M(카나리·ASLR 기본 없음)에서 DoS~코드실행 잠재. **심각도 Medium.** 범위: Sensor·Drive uds.c.
- **근본 원인:** ECU가 0x34에서 maxBlockLen=258(데이터 256)을 *광고*하나 0x36에서 **블록 크기를 집행 안 함**. endless-data 누적가드(FR-CAN-012)는 *누적 vs 선언*만 봐 *블록 vs 버퍼* 사각지대를 못 막음.
- **조치:** `chunk_len > 256` → NRC 0x31 가드(양 ECU). 회귀 `test_transfer_data_oversized_block`(test_uds_state) 추가.
- **검증:** 원 PoC(261B) 재생 **클린** · **재퍼징 1.4M회 무크래시**(동일 퍼저가 더는 미발견) · `test_uds_state` **30/30** PASS. → [ISS-SEC-004](../troubleshooting/ISS-SEC-004_uds-transferdata-per-block-overflow.md) 8D.
- **의의:** **커버리지 가이드 퍼징이 endless-data 수정이 놓친 신규 메모리 손상(CWE-121)을 1초 미만에 발굴·차단** — FH-3의 핵심 성과.

## 8. 추적성

| 퍼즈 타깃 | 요구 | 위협(TARA) | 결과/Finding |
|---|---|---|---|
| FH-1 헤더 파서 | SR-FW-002·FR-AB-008·FR-CAN-011 | T-1·T-3·T-6 | F-001(B) — 실로직 분기 커버, 운영결함 0 |
| FH-2 메타 파서 | FR-AB-005 | — | ⬜ 계획 |
| FH-3 UDS 다운로드 | FR-CAN-012/013·SR-ATK-007 | T-8 | ✅ **F-003**(per-block 스택오버플로 CWE-121) 발견·수정·재퍼징 클린 + **v2 다중블록·SC3로 endless-data 누적가드+플래시모델 입증** |

전체 추적은 [RTM-001](../requirements/RTM-001_Requirements_Traceability_Matrix.md)에 통합 예정.

## 9. 개정 이력

| 버전 | 날짜 | 내용 |
|---|---|---|
| 0.1 | 2026-06-07 | 최초 — SEC.3/21434 §10.4/29119-3 골격 수립. 전략(§2)·타깃 명세 FH-1~3(§3)·트리아지 절차(§4)·처리 루프(§5) 정의. **FH-1 `harness_imghdr` 1차 실행**, F-001(헤더 over-read) 발견·(B) 분류·하니스 가드 조치 기록 |
| 0.2 | 2026-06-07 | **FH-1 완료** — 가드 적용 후 2,988만 회 무크래시·커버리지 정체로 중단기준 충족(§6). **§3 타깃을 리스크 기반 재정렬**(노출×복잡도×결과) — 다음 우선 **FH-3**(T-8 endless-data, P1), FH-2 후순위(P2), FH-1 완료(P3). 우선순위 근거 명시 |
| 0.3 | 2026-06-08 | **FH-3 sanity 통과**(프라이밍→DOWNLOADING→0x76) + **F-002 발견·수정** — sha256.c 부호 있는 시프트 UB를 UBSAN이 검출(§7), 3개 사본 `(WORD)` 캐스트, 회귀(test_hmac·test_uds_state) 통과, ISS-SEC-003 발행. 퍼즈 본실행은 다음 |
| 0.4 | 2026-06-08 | **FH-3 본실행 — F-003 발견·수정·검증** — 261B 블록이 `padded[260]` 스택오버플로(CWE-121, uds.c:252)를 <1초에 유발. 양 ECU `chunk_len>256→NRC 0x31` 가드 + 회귀 테스트. 원 PoC 재생 클린·재퍼징 1.4M회 무크래시·test_uds_state 30/30. ISS-SEC-004 발행. FH-3 ✅ |
| 0.5 | 2026-06-08 | **FH-3 v2(다중블록) + SC3 리그 입증** — endless-data 누적 경로 퍼징(1.23M회·cov 253) 가드 있음 무크래시(방어 성립). SC3: 누적가드 일시 제거 시 **플래시모델이 OOB 검출**(16384B 경계), 복원 후 무크래시 → 리그·FR-CAN-012 가드 유효 입증. 슬롯 16KB 축소 모델 주(§3) |
