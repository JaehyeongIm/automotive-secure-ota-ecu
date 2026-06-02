# Troubleshooting Log

## Phase 8 — OTA 실패 후 SensorECU heartbeat 없음 (ISS-OTA-005)

**최초 관측:** 2026-05-27  
**최종 정리:** 2026-05-28  
**상태:** 증상 문서로 유지, 정본은 `ISS-OTA-004`

---

### 이 문서의 역할

이 문서는 `TIMEOUT: no heartbeat from sensor ECU on can0` 증상으로 현장에서 바로 찾기 위한 **증상 진입 문서**다.

이번 phase 8 이슈의 최종 원인, 오진했던 가설, 진단 로그 추가 과정, 수정, 재발 방지 대책의 정본은 아래 문서에 정리한다.

- [ISS-OTA-004_sensor-ota-random-timeout.md](ISS-OTA-004_sensor-ota-random-timeout.md)

---

### 현상

OTA 실패 후 다음 증상이 반복적으로 보일 수 있었다.

```text
TIMEOUT: no heartbeat from sensor ECU on can0
```

또는 UART/부트로더 기준으로는:

```text
[BL] Slot B selected
[BL] ECDSA OK
[BL] Jump to 0x08040000 ...
... 이후 SensorECU 앱 기동 로그 없음
```

현장에서는 메타데이터 erase + Slot A 재플래시로 복구되는 경우가 있어, 처음에는 메타데이터 불일치나 safe-state 진입이 주원인처럼 보일 수 있다.

---

### 최종 결론

이번 heartbeat 소실은 **별도 CAN 문제**나 **별도 메타데이터 문제**가 아니라, `ISS-OTA-004`와 같은 **slot bin 재사용 빌드 버그의 파생 증상**이었다.

핵심은 다음과 같다.

- Slot B로 OTA했다고 생각한 이미지가 실제로는 Slot A 주소로 링크된 raw bin일 수 있었음
- 그 상태에서 부트로더가 Slot B를 선택해도, vector table 안 reset handler는 Slot A 범위를 가리킬 수 있었음
- 결과적으로 reboot 후 SensorECU 앱이 정상 기동하지 못하고 heartbeat가 사라짐

대표 실패 증거:

```text
[BL] Slot B selected
[BL] Jump to 0x08040000  SP=0x20020000  PC=0x080138CD
```

정상 복구 후:

```text
[BL] Slot B selected
[BL] Jump to 0x08040000  SP=0x20020000  PC=0x08044145
[SensorECU v1] Start, Slot=1
```

---

### 빠른 판별 포인트

heartbeat가 안 돌아오면 아래 순서로 먼저 본다.

1. Bootloader가 어떤 slot을 선택했는지 확인
2. `Jump to ... PC=...` 주소가 선택 slot 범위와 일치하는지 확인
3. `0x77`까지 갔는지 확인해서 transport 단계와 post-reboot 단계를 분리
4. Slot A/B artifact 해시와 vector table을 먼저 검증

즉, 이 증상은 메타데이터만 보지 말고 **selected slot / reset handler / artifact lineage**를 함께 봐야 한다.

---

### 왜 복구 절차가 원인처럼 보였나

메타데이터 erase + Slot A 재플래시는 실제로 heartbeat를 되살릴 수 있었다. 하지만 이것은 근본 원인을 고친 것이 아니라, **부팅 가능한 self-consistent 상태를 강제로 만든 것**에 가깝다.

1. 메타데이터 erase
2. 부트로더가 잘못된 active slot 정보를 덜 신뢰
3. Slot A를 직접 재플래시
4. 정상 vector table과 코드가 들어가면서 heartbeat 복귀

즉 recovery는 유효했지만, root cause는 메타데이터 자체보다 **잘못 만들어진 slot 이미지**였다.

---

### 재발 방지 메모

- heartbeat timeout 문서에는 항상 Bootloader `selected slot / SP / PC` 로그를 함께 남긴다
- post-reboot heartbeat 실패가 보이면 CAN 전에 artifact mismatch를 먼저 의심한다
- 이 이슈의 상세 타임라인과 대책은 정본 문서 하나에서만 유지한다

---

### 관련 파일

- `ci/build.sh` — stale `.bin` 재사용 제거
- `Bootloader/Core/Src/bootloader.c` — slot 선택 후 vector table jump
- `SensorECU/STM32F446RETX_FLASH.ld`
- `SensorECU/STM32F446RETX_FLASH_SlotB.ld`

---

### 정본 문서

- [ISS-OTA-004_sensor-ota-random-timeout.md](ISS-OTA-004_sensor-ota-random-timeout.md) — phase 8 전체 분석, 최종 원인, 해결, 재발 방지 대책
