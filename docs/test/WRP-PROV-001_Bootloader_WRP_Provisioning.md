# WRP-PROV-001: Bootloader WRP 프로비저닝 & 검증 절차

| 항목 | 내용 |
|---|---|
| 문서 ID | WRP-PROV-001 |
| 레벨 | on-target 보안 프로비저닝 + 검증 (ASPICE SWE.6, ISO/SAE 21434, NIST SP 800-193) |
| 대상 | DriveECU + SensorECU (STM32F446RE) — 부트로더 영역(섹터 0~1) |
| 작성일 | 2026-06-09 |
| 추적 | FR-BL-013(WRP), SR-KEY-001/003, [TARA-001](../security/TARA-001_Threat_Analysis_Risk_Assessment.md) T-2·CG-2 |

> 부트로더(섹터 0~1)에는 ECDSA 공개키([SR-KEY-001](../requirements/RTM-001_Requirements_Traceability_Matrix.md))와 SecurityAccess PSK([SR-KEY-003](../requirements/RTM-001_Requirements_Traceability_Matrix.md), `0x08007FE0`)가 들어 있다. 이 영역을 STM32 WRP(Write Protection)로 쓰기보호해 펌웨어·키의 **변조(무결성 침해)를 차단**한다.
> 근거: NIST SP 800-193(펌웨어 무단변조 방지·immutable Root of Trust), ISO/SAE 21434(RoT 무결성). **범위는 무결성(쓰기보호)** — PSK readout(기밀성)은 §6 참조.

---

## 1. 원리 — WRP는 칩마다 따로

WRP는 각 MCU 내부 옵션바이트(FLASH_OPTCR @`0x40023C14`)에 저장되는 **칩 개별 속성**이다. 공유/브로드캐스트되지 않는다.

- ST-Link 1개 = 타깃 칩 1개. CLI는 한 번에 프로브 하나에만 붙는다.
- **2보드 동시 연결 시 `port=SWD`만 쓰면 먼저 열거된 한 칩만** 잡힌다 → 반드시 `sn=`으로 칩을 지정한다.
- `nWRPi = 0` → 섹터 i **쓰기보호 활성**, `= 1` → 해제. (`SPRMOD=0` 전제: nWRP가 쓰기보호로 동작, PCROP 아님)
- 두 ECU는 **동일 메모리맵**(링커 `FLASH: 0x8000000, 32K` → 섹터 0~1)이라, drive/sensor 구분 없이 같은 설정을 각 칩에 적용한다.

---

## 2. 사전 조건

- **STM32CubeProgrammer CLI** (GUI는 닫을 것 — GUI가 ST-Link를 점유하면 CLI `port=SWD` 연결 실패)
  ```bash
  alias stm32cli="/Applications/STMicroelectronics/STM32Cube/STM32CubeProgrammer/STM32CubeProgrammer.app/Contents/Resources/bin/STM32_Programmer_CLI"
  ```
  > brew `st-flash`/`st-info`는 옵션바이트 WRP를 다루지 못한다 — 반드시 CubeProgrammer CLI 사용.
- **보드 시리얼** (재연결 시 변동 가능 — `stm32cli --list`로 확인)

  | ECU | ST-Link SN |
  |---|---|
  | DriveECU | `0669FF485775495067211743` (…43) |
  | SensorECU | `066EFF485775495067194557` (…57) |

---

## 3. 절차 (보드당 반복 — `<SN>`만 교체)

**0단계 — 현재 옵션바이트 확인 (읽기 전용, 안전)**
```bash
stm32cli -c port=SWD sn=<SN> -ob displ
```
→ `RDP : 0xAA (Level 0)`, `SPRMOD : 0x0`, `nWRP0~7 : 0x1(해제)` 확인. **RDP가 0xAA가 아니면 중단하고 점검.**

**1단계 — WRP 설정 (섹터 0~1만)**
```bash
stm32cli -c port=SWD sn=<SN> -ob nWRP0=0 nWRP1=0
```
→ `Option Bytes successfully programmed` 기대. (비파괴 — 플래시 내용 보존, 가역적)

**2단계 — 적용 확인**
```bash
stm32cli -c port=SWD sn=<SN> -ob displ | grep -E "nWRP[01] |RDP"
```
→ `nWRP0/1 : 0x0 (Write protection active)`, `RDP : 0xAA` 유지.

**3단계 — write 거부 입증 (FR-BL-013 합격기준)**
```bash
stm32cli -c port=SWD sn=<SN> -e 0
```
→ 섹터 0(부트로더) erase가 **거부**되어야 한다. WRP active라 `WRPERR`로 막히며, 부트로더는 지워지지 않는다.

---

## 4. 검증 결과 (2026-06-09, on-target)

양 보드에 1~3단계 수행. 변경 전 erase는 성공 / 변경 후 nWRP active 확인 / 변경 후 erase 거부 → 원인은 WRP로 확정.

| 보드 (SN) | nWRP0 | nWRP1 | RDP | 섹터0 erase |
|---|---|---|---|---|
| DriveECU (`…43`) | `0x0` active | `0x0` active | `0xAA` (미변경) | **거부** ✅ |
| SensorECU (`…57`) | `0x0` active | `0x0` active | `0xAA` (미변경) | **거부** ✅ |

3단계 실제 출력(양 보드 동일):
```
Erase sector(s) ...
Error: Sector erase operation failed at least for one of the existing specified sectors
```

**판정: FR-BL-013 PASS** — 부트로더 영역 쓰기보호가 양 보드에서 실제로 동작함을 on-target 입증.

---

## 5. 재플래시 워크플로 (WRP 발효 후)

WRP active 상태에선 섹터 0~1 write/erase가 차단된다. **부트로더/PSK를 갱신**하려면:

```bash
stm32cli -c port=SWD sn=<SN> -ob nWRP0=1 nWRP1=1   # 1) 해제(비파괴)
tools/flash.sh <drive|sensor> <SN> --bl            # 2) 부트로더 재플래시
stm32cli -c port=SWD sn=<SN> -ob nWRP0=0 nWRP1=0   # 3) 재보호
```

- ⚠️ [`tools/flash.sh --bl`](../../tools/flash.sh)은 `0x08000000`을 st-flash로 쓰므로 **WRP active면 실패** → 위 1·3단계를 수동으로 감싸야 한다.
- **기본 `flash.sh`(앱+메타, 섹터 2·4~7)와 App OTA(CAN/UDS)는 WRP와 무관** — 평소대로 동작.
- CubeIDE Debug 버튼으로 부트로더 플래시 금지([EXP-001](../troubleshooting/EXP-001_primask-vtor-isolation.md)) — 이제 "WRP 해제/재설정 절차 필요"가 사유에 추가됨.

---

## 6. 잔여 위험 & 후속 (정직 표기)

WRP는 **쓰기보호(무결성)**만 제공한다. **readout(기밀성)은 막지 못한다**:

| 자산 | WRP로 충분? | 후속 |
|---|---|---|
| ECDSA 공개키 | ✅ (공개라 변조만 막으면 됨) | — |
| PSK(대칭키) | ⚠️ 변조는 차단, **SWD readout 덤프는 미차단**([TARA T-2](../security/TARA-001_Threat_Analysis_Risk_Assessment.md)) | **RDP Level 1**(readout 차단) |

- **RDP**: 이 절차는 RDP를 건드리지 않는다(0xAA Level 0 유지). PSK 기밀성을 닫으려면 RDP Level 1 도입 — 단 강등 시 mass erase·재플래시 마찰 트레이드오프.
- **PSK 로테이션**: 현재 PSK는 git에 평문 커밋된 dev placeholder. RDP 도입 시 랜덤값 교체 + git 제외 + ST-Link 주입([SR-KEY-004](../requirements/RTM-001_Requirements_Traceability_Matrix.md))이 짝으로 와야 의미가 있다.
- **물리 추출 방어**: Secure Element([ADR-006](../design/adr/ADR-006_Secure_Element_Adoption.md)).

---

## 7. 주의 — RDP 미변경 원칙

이 절차는 **WRP만** 다룬다. RDP는 `0xAA`(Level 0) 유지. 특히 **RDP `0xCC`(Level 2)는 비가역**(디버그 포트 영구 비활성)이니 절대 설정하지 말 것. WRP 토글 자체는 비파괴라 안전하다.
