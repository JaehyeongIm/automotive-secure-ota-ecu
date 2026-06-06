# ISS-OTA-006: OTA TransferData 중 "No Flow Control from ECU" — RX FIFO 오버런(고부하)

> 트러블슈팅(8D 경량판). on-target OTA 전송 중 발견. **진행 중 — 정본 원인 확정/근본수정 후속.**

| 항목 | 내용 |
|---|---|
| 증상 | OTA TransferData가 임의 블록(이번 ~59%, block 83/139)에서 멈추고 Pi가 `No Flow Control from ECU`. ECU는 살아있음(heartbeat 지속) |
| 상태 | 🔶 완화 확정(`--cf-delay 0.02` → 139/139 전량 전송 OK, 2026-06-07), 근본수정 후속 |
| 심각도 | Medium–High (OTA 신뢰성) |
| 영향 | ECU ISO-TP/CAN RX 고부하 경로(`isotp.c`/CAN RX) |
| 관련 | **ISS-OTA-004 §남은 메모**(FIFO0 FULL/OVERRUN deferred), FR-CAN-006/012, `tools/ota_client.py --cf-delay` |

---

## D2. 문제 정의 (5W2H · Is–Is Not)

- **무엇:** SecurityAccess unlock·RequestDownload는 정상. TransferData 139블록 중 ~83에서:
  ```
  [UDS]    59%  block=83
  [OTA] ERROR: No Flow Control from ECU
  ```
  ECU UART는 `[UDS] Block 83  21248/35428`까지 찍고 이후 `[ALIVE]`로 복귀 — **크래시·리셋 아님**(부트 배너 없음, ESR=0).
- **얼마나:** 실패 블록 번호가 매번 다름(랜덤). 각 0x36 블록(256B)은 그 자체로 멀티프레임 ISO-TP(FF+CF) → 다음 블록 FF에 ECU가 **Flow Control(FC)을 안 보냄** → Pi 타임아웃.
- **Is:** 대용량(35KB)·연속프레임 고부하 전송 중. **Is Not:** unlock/0x34 단계, 소량 전송.

## D4. 근본 원인 분석 (잠정 가설)

- **이번 증상은 ISS-OTA-004의 빌드-산출물 버그가 아니다** — 그건 `0x34` 즉시 timeout 또는 재부팅 후 heartbeat 소실로 나타났고 이미 수정됨. 본 건은 **전송 중간** FC 미발신이라 transport-layer 문제.
- 유력 가설: **bxCAN RX FIFO(깊이 3) 오버런** — 연속프레임이 빠르게 도착하는데 ECU가 제때 못 비우면(특히 **블록별 `[UDS] Block N` blocking UART printf**가 CAN RX 서비스 시간을 잠식) CF 1개 유실 → 해당 블록 ISO-TP 재조립이 어긋나거나 다음 블록 FC 미발신 → Pi `No Flow Control`. 랜덤 블록에서 터지는 것과 정합.
- **ISS-OTA-004가 "별도 robustness 과제"로 분리해 둔 `CAN RX FIFO0 FULL/OVERRUN`이 바로 이 클래스** — phase 8에선 주원인이 아니라 미뤘던 항목.
- ⚠️ 미확정: cf-delay별 통과율·FIFO overrun 카운터로 가설 검증 필요.

## D5–D6. 시정 조치 (진행)

- **완화(펌웨어 무변경) — 검증됨:** `ota_client.py --cf-delay 0.005 → 0.02`(CF 간격↑ → ECU FIFO 드레인 여유). **2026-06-07 재현: cf-delay 0.02로 139/139 블록 완주 + SIT-TC-01 anti-rollback on-target PASS.** 기본값(0.005)에선 여전히 랜덤 실패(~59%, block 83)이므로 *완화*이지 근본수정은 아님.
- **근본수정(후속, 펌웨어):**
  - OTA 중 블록별 `[UDS] Block N` printf 축소/플래그화(`DEBUG_OTA_DIAG`) — blocking UART가 CAN RX를 막지 않게.
  - CAN RX FIFO0 인터럽트로 즉시 드레인 + 오버런 플래그 복구.
  - (대안) `ota_client.py` 기본 `--cf-delay`를 0.005→0.01로 상향(안정성↑·속도↓ trade-off).
- **검증(추가 예정):** cf-delay(5/10/20ms)별 통과율, 대용량 A↔B 왕복 OTA regression, FIFO overrun 카운터 관측.

## D7. 재발 방지 (Lessons Learned)

- OTA regression에 **대용량(≈35KB) 전송**을 기본 포함(소량만 테스트하면 이 클래스가 안 드러남).
- ISR/고부하 경로에서 **blocking printf 금지** 원칙(ISS-OTA-004의 RX ISR printf 제거와 동일 교훈) — 진단 로그는 컴파일 플래그로.
- FIFO overrun 카운터를 상시 관측 지표로(은폐 말고 계측).
- 표준: ISO 15765-2(ISO-TP Flow Control/STmin/BS), bxCAN RX FIFO(RM0390).
