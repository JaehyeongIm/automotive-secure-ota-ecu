# Troubleshooting Log

## Phase 8 — SensorECU OTA 랜덤 타임아웃 (ISS-OTA-004)

**최초 관측:** 2026-05-26  
**최종 정리:** 2026-05-28  
**상태:** 해결 완료

---

### D2. 문제 정의 — 현상

SensorECU OTA가 아래 두 형태로 불규칙하게 실패했다.

1. `RequestDownload(0x34)` 직후 `Receive timeout`
2. `RequestTransferExit(0x37)`까지 끝났는데 재부팅 후 heartbeat가 돌아오지 않음

실패 블록 번호가 매번 달라 보여 처음에는 CAN 혼잡, TX 메일박스 포화, `AutoRetransmission`, CF 간격 문제처럼 보였다.

대표 증상:

```text
[UDS] RequestDownload  size=29840 bytes
[FAIL] Receive timeout
```

또는

```text
[UDS][37] queue 0x77
[UDS] OTA done, rebooting to Slot B
... 이후 Slot B heartbeat 없음
```

---

### 왜 어려웠나

이번 이슈가 특히 어려웠던 이유는 증상과 실제 원인 사이의 거리가 멀었기 때문이다.

#### 1. CubeIDE 직접 플래시는 정상인데 OTA만 실패했다

로컬에서 ST-Link / CubeIDE로 펌웨어를 직접 플래시했을 때는 앱이 정상 동작했다. 그래서 자연스럽게 다음과 같이 생각하기 쉬웠다.

- 펌웨어 자체는 정상
- OTA 전송 경로나 CAN 타이밍이 문제

하지만 실제로는 **펌웨어 소스가 아니라 slot artifact 생성 과정**이 문제였다. 즉, "직접 플래시한 이미지"와 "OTA로 전달된 이미지"의 생성 경로가 달랐고, 버그는 그 차이에 숨어 있었다.

#### 2. 실패가 비결정적으로 보였다

관측된 현상은 매우 비일관적이었다.

- 파이프라인 1회차는 성공하고 3회차부터 실패
- `RequestDownload` 에서 바로 timeout
- `TransferData` 중 서로 다른 block 번호에서 timeout
- `0x77` 까지 끝났는데 reboot 후 heartbeat 없음

이런 패턴 때문에 처음에는 운이 나쁜 CAN 타이밍 문제처럼 보였다.

#### 3. 증상은 CAN 문제처럼 보이는 신호를 계속 만들었다

실패 로그에는 실제로 다음 같은 단서가 있었다.

- `CAN RX FIFO0 FULL`
- `CAN RX FIFO0 OVERRUN`
- `CAN ERROR`
- heartbeat timeout
- `Receive timeout`

이 신호들은 모두 진짜였지만, **근본 원인**은 아니었다. 그래서 메일박스, `AutoRetransmission`, CF 간격, 센서 측정 블로킹 같은 가설이 반복해서 유력해 보였다.

#### 4. 호스트 로그만으로는 원인을 구분할 수 없었다

처음에는 Raspberry Pi 터미널 로그만 가지고 원인을 찾으려 했다. 하지만 호스트 관점에서 보이는 것은 아래 정도뿐이다.

- `0x74`를 못 받았다
- `0x77`를 받았지만 heartbeat가 안 돌아왔다
- 몇 번째 block에서 timeout 났다

이 정보만으로는 아래 셋을 구분하기 어렵다.

1. OTA 전송 자체가 깨졌는가
2. ECU가 쓰기/응답 단계에서 죽었는가
3. reboot 후 잘못된 이미지를 부팅했는가

#### 5. AI 보조 분석도 입력이 부족하면 잘못된 가설을 강화했다

이번 사례에서 중요한 교훈은, **어떤 AI 도구를 쓰느냐보다 어떤 증거를 주느냐가 더 중요했다**는 점이다.

- 호스트 로그만 준 상태에서는 CAN/메일박스 쪽 가설이 계속 강화됐다
- "원인을 맞혀 달라"보다 "어떤 로그를 더 넣어야 가설을 구분할 수 있나"를 묻는 방향이 더 효과적이었다
- UART/Bootloader 진단 로그를 추가한 뒤에야 원인이 빠르게 좁혀졌다

즉, 이번 해결의 전환점은 AI가 답을 바로 낸 것이 아니라, **진단 가능성을 높이는 로그를 먼저 설계한 것**이었다.

---

### D4. 근본 원인 (최종 원인)

근본 원인은 CAN 타이밍이 아니라 **SensorECU slot 산출물 생성 오류**였다.

`SensorECU`는 Slot A와 Slot B를 같은 `Debug` 디렉터리에서 빌드한다. 그런데 `ci/build.sh`는 한동안 다음 조건으로 `.bin` 생성을 건너뛰고 있었다.

- `make clean` 후 새 ELF 생성
- 기존 `SensorECU.bin` 이 이미 있으면 `objcopy` 생략

이때 `make clean`은 `.elf`, `.o` 등은 지우지만 `SensorECU.bin`은 지우지 않았다. 그 결과:

1. Slot A 빌드가 `Debug/SensorECU.bin` 생성
2. Slot B 빌드는 ELF만 새로 링크
3. 기존 `SensorECU.bin`이 남아 있어서 Slot B용 `objcopy` 생략
4. `artifacts/sensor_slotB.bin`에 **Slot A용 raw bin** 이 복사됨

즉, **Slot B 이미지라고 서명하고 OTA한 파일이 실제로는 Slot A 주소로 링크된 바이너리**였다.

---

### 실제 해결 과정

이번 이슈는 "원인을 바로 찾았다"기보다, 잘못된 가설을 하나씩 제거하면서 **관측 지점을 늘린 끝에** 해결됐다.

#### 1. 처음에는 전송 계층 문제로 봤다

초기 해석은 다음 순서였다.

1. CAN TX 메일박스 경합
2. `AutoRetransmission = DISABLE`
3. CF 간격 부족
4. hcsr04 측정 블로킹

이 해석이 설득력 있어 보였던 이유는, timeout과 heartbeat 소실이 실제로 transport 문제처럼 보였기 때문이다.

#### 2. "원인을 맞히기"보다 "가설을 구분하는 로그"를 넣기 시작했다

전환점은 SensorECU UART에 아래 진단 로그를 추가한 것이었다.

- `0x34` 시작, erase 시작/완료, `0x74` 큐잉 로그
- `0x37` 시작, 메타데이터 기록 결과, `0x77` 큐잉 로그
- OTA 중 TX fail 여부 (`TX_FAIL_DURING_OTA`)
- CAN FIFO full / overrun / error 스냅샷
- Bootloader의 `SP`, `PC`, 선택된 slot 로그

이 로그가 들어가면서 비로소 다음이 구분됐다.

- OTA 데이터 전송은 끝났는가
- ECU가 reboot 전에 죽었는가
- reboot 후 어떤 slot을 선택했고, 실제 어디로 jump 했는가

#### 3. 빌드 산출물 자체를 비교했다

추가 로그를 본 뒤, Raspberry Pi 빌드 출력에서 다음 이상 징후가 확인됐다.

- `sensor_slotA.bin` 과 `sensor_slotB.bin` 의 SHA-256 동일
- 그런데 Slot A/B linker origin은 서로 다름

이 시점부터 원인은 transport가 아니라 build artifact 생성 쪽으로 급격히 좁혀졌다.

#### 4. Bootloader jump 주소가 결정적 증거가 됐다

실패 시:

```text
[BL] Slot B selected
[BL] Jump to 0x08040000  SP=0x20020000  PC=0x080138CD
```

성공 시:

```text
[BL] Slot B selected
[BL] Jump to 0x08040000  SP=0x20020000  PC=0x08044145
```

이 차이는 "전송이 실패했다"가 아니라, **잘못된 이미지를 성공적으로 전송해 놓고 reboot 후 잘못 실행했다**는 뜻이었다.

#### 5. `ci/build.sh` 수정 후 증상이 한 번에 정리됐다

shared build dir에서 stale `.bin` 을 재사용하지 않도록 바꾼 뒤:

- `count=1` 통과
- `count=3` 통과
- `0x77` 후 heartbeat 복귀
- Slot A/B jump address 정상화

즉 그동안 separate issue처럼 보였던 timeout, reboot 후 heartbeat 없음, safe-state 복구 필요 현상이 모두 하나의 build 문제로 연결됐다.

---

### 왜 랜덤 타임아웃처럼 보였는가

이 빌드 문제는 OTA 단계에 따라 서로 다른 형태로 드러났다.

#### 1. `0x34` 직후 timeout

메타데이터상 `active_slot=1` 이면 다음 OTA 타깃은 Slot A가 된다. 그런데 이전 OTA로 기록된 Slot B 이미지가 실제로는 Slot A 주소에 의존하고 있으면, Slot A erase가 **실행 중인 코드 경로를 건드릴 수 있다**.

이 경우 ECU가 `0x74` 응답 전에 리셋되거나 멈춰서, 호스트에서는 단순히 `Receive timeout`으로 보인다.

#### 2. `0x77`까지 성공했는데 reboot 후 heartbeat 없음

이 경우 OTA 데이터 전송과 메타데이터 기록은 성공한다. 하지만 재부팅 후 부트로더가 Slot B를 선택해도, Slot B 벡터 테이블 안 reset handler가 Slot A 주소를 가리키고 있으면 앱이 정상적으로 올라오지 못한다.

그래서 호스트 쪽에서는 `0x77`까지 받은 뒤 `Slot B heartbeat timeout`으로 관측된다.

---

### 오진했던 가설

다음 가설들은 증상 설명에는 일부 도움이 됐지만, 이번 장애의 근본 원인은 아니었다.

- CAN TX 메일박스 포화
- `AutoRetransmission = DISABLE`
- CF 간격(`--cf-delay`) 부족
- hcsr04 측정 블로킹
- CAN bus-off 또는 host CAN 인터페이스 불안정

이 가설들이 약해진 이유:

- 수정 전 실패 로그에서도 `Slot A`와 `Slot B` 산출물 SHA-256이 완전히 동일했다.
- 수정 후에도 UART에 `CAN RX FIFO0 FULL` / `OVERRUN` 로그는 남아 있었지만 OTA는 `count=1`, `count=3` 모두 통과했다.
- `can0_before.txt`, `can0_after.txt` 모두 `ERROR-ACTIVE`, `bus-off=0` 상태였다.

즉 FIFO 경고는 **남아 있는 robustness 이슈**일 수는 있어도, 이번 phase 8 장애의 주원인은 아니었다.

---

### 결정적 증거

#### 1. 실패 시 Slot A/B raw bin 해시가 동일

실패 당시 Raspberry Pi 빌드 로그:

```text
[SIGN] SHA-256: c08b6473bdece1097c1ff2b774a1e517ea4e36d5ea0d37bc51b04ef0844109cf
```

이 값이 `sensor_slotA.bin` 과 `sensor_slotB.bin` 에서 동일하게 나왔다.

하지만 linker origin은 서로 다르다.

- Slot A: `SensorECU/STM32F446RETX_FLASH.ld` → `FLASH ORIGIN = 0x08010000`
- Slot B: `SensorECU/STM32F446RETX_FLASH_SlotB.ld` → `FLASH ORIGIN = 0x08040000`

정상이라면 raw bin 해시가 같을 수 없다.

#### 2. 실패 시 Bootloader가 Slot B를 고르는데 PC는 Slot A 범위

실패 UART 로그:

```text
[BL] Slot B selected
[BL] Jump to 0x08040000  SP=0x20020000  PC=0x080138CD
```

Slot B를 부팅하는데 reset handler PC가 `0x080138CD` 인 것은, Slot B 플래시에 들어 있는 이미지가 Slot A 주소로 링크됐다는 뜻이다.

#### 3. 수정 후 PC가 각 슬롯 범위로 정상화

해결 후 UART 로그:

```text
[BL] Slot B selected
[BL] Jump to 0x08040000  SP=0x20020000  PC=0x08044145
[SensorECU v1] Start, Slot=1
```

```text
[BL] Slot A selected
[BL] Jump to 0x08010000  SP=0x20020000  PC=0x08014145
[SensorECU v1] Start, Slot=0
```

이제 reset handler가 선택된 슬롯 범위와 일치한다.

---

### D5–D6. 시정 조치

`ci/build.sh`에서 shared build dir에 남아 있는 stale `.bin` 을 재사용하지 않도록 수정했다.

수정 전:

```bash
if [ ! -f "$BUILD_DIR/$PROJ.bin" ]; then
  arm-none-eabi-objcopy -O binary "$BUILD_DIR/$PROJ.elf" "$BUILD_DIR/$PROJ.bin"
fi
```

수정 후:

```bash
make -C "$BUILD_DIR" clean
make -C "$BUILD_DIR" -j"$(nproc)" all

# Shared build dirs can leave a stale .bin behind, so always regenerate from
# the freshly linked ELF for the selected slot.
arm-none-eabi-objcopy -O binary "$BUILD_DIR/$PROJ.elf" "$BUILD_DIR/$PROJ.bin"
```

핵심은 **slot별 ELF를 링크한 직후 항상 그 ELF에서 raw bin을 다시 생성**하는 것이다.

---

### D5–D6. 검증 결과

2026-05-28 재검증:

- `count=1` OTA 테스트 통과
- `count=3` OTA 테스트 통과
- `ota_stress.log` 기준 `3/3 통과`
- 매 iteration에서 `0x77` 수신 후 heartbeat 대기 `OK`
- UART 기준 `TX_FAIL_DURING_OTA=0`

요약:

```text
Iteration 1: PASS
Iteration 2: PASS
Iteration 3: PASS
합계: 3/3 통과
```

---

### 남은 메모

UART에는 여전히 아래 로그가 간헐적으로 보인다.

```text
[CAN RX FIFO0 FULL]
[CAN RX FIFO0 OVERRUN]
[CAN ERROR]
```

하지만 현재 로그 기준으로는:

- OTA 전송 완료
- `0x77` 응답 정상
- 슬롯 전환 후 부팅 정상
- heartbeat 복귀 정상

따라서 이는 **별도 성능/안정성 개선 과제**로 보고, phase 8 장애의 원인과는 분리해서 다루는 것이 맞다.

---

### 다음에 같은 문제를 푸는 방법

다음에 "직접 플래시는 되는데 OTA만 비결정적으로 실패"하는 문제가 나오면, 아래 순서로 접근한다.

#### 1. 먼저 실패 단계를 세 구간으로 나눈다

문제를 한 덩어리로 보지 말고 아래 세 구간으로 나눈다.

1. **artifact 생성**: 어떤 이미지를 만들었는가
2. **transport / write**: ECU가 그 이미지를 어디까지 받았는가
3. **post-reboot**: reboot 후 실제 어떤 주소로 jump 했는가

이 세 구간을 분리하지 않으면, build 문제를 transport 문제로 오해하기 쉽다.

#### 2. 최소 수집 로그 세트를 고정한다

다음 장애부터는 아래 로그를 항상 같이 모은다.

- Raspberry Pi `ota_stress.log`
- `candump_sensor_ota.log`
- `can0_before.txt`
- `can0_after.txt`
- SensorECU UART 로그
- 가능하면 Bootloader UART 로그

특히 **Bootloader의 selected slot / SP / PC 로그**는 필수다.

#### 3. Slot A/B artifact부터 먼저 검증한다

CAN 분석 전에 먼저 아래를 확인한다.

- `sensor_slotA.bin` 과 `sensor_slotB.bin` 해시가 다른가
- 각 slot ELF/vector table의 reset handler가 해당 slot 주소 범위에 있는가
- signing 대상이 기대한 slot artifact와 일치하는가

직접 플래시는 되는데 OTA만 안 되면, transport보다 먼저 **artifact lineage**를 의심하는 편이 더 빠르다.

#### 4. `0x74`, `0x77`, heartbeat를 각각 다른 단계의 증거로 본다

- `0x74` 없음 → erase 이전/직후 문제 가능성
- `0x77` 있음 → OTA write/metadata 단계는 대체로 통과
- reboot 후 heartbeat 없음 → post-reboot 이미지/slot 선택 문제 가능성 높음

이 셋을 한 종류의 timeout으로 묶어버리면 원인 분리가 안 된다.

#### 5. AI에게는 "답"보다 "구분 실험"을 요청한다

다음처럼 묻는 편이 효과적이다.

- "이 증상을 build / transport / boot 중 어디로 나누면 좋을까?"
- "이 가설들을 구분하려면 어떤 로그를 추가해야 하나?"
- "이 로그에서 빠진 결정적 관측점은 무엇인가?"

이 방식이 "원인을 바로 맞혀 달라"보다 훨씬 재현성이 높다.

---

### D7. 재발 방지 대책

#### 1. Build 파이프라인 방어

- slot별 ELF에서 raw `.bin`을 항상 재생성한다
- CI에서 `sensor_slotA.bin` 과 `sensor_slotB.bin` 해시가 동일하면 실패 처리한다
- 가능하면 slot별 vector table의 reset handler가 기대 주소 범위에 있는지 자동 검사한다

#### 2. OTA 검증 자동화

- `count=1` smoke test와 `count=3` 반복 테스트를 둘 다 유지한다
- A→B, B→A 왕복 OTA를 기본 regression 시나리오로 둔다
- `0x77` 수신만이 아니라 reboot 후 expected slot heartbeat까지 성공 조건에 포함한다

#### 3. 관측성 유지

- `0x34` / `0x37` / boot jump 진단 로그는 완전히 제거하지 말고 유지한다
- 필요하면 `DEBUG_OTA_DIAG` 같은 compile-time flag로 관리한다
- host 로그와 UART 로그를 같은 테스트 세트로 보관하는 습관을 유지한다

#### 4. 문서화 원칙

- 증상 문서와 최종 원인 문서를 분리하지 말고, "왜 오해했는가"까지 같이 남긴다
- 복구 절차가 원인처럼 보일 때는, 복구가 왜 먹혔는지도 별도로 적는다
- AI가 틀렸는지보다, 어떤 로그를 줬을 때 틀렸는지를 기록한다

---

### 관련 파일

- `ci/build.sh` — slot별 `.bin` 산출물 생성
- `SensorECU/STM32F446RETX_FLASH.ld` — Slot A 링크 주소
- `SensorECU/STM32F446RETX_FLASH_SlotB.ld` — Slot B 링크 주소
- `Bootloader/Core/Src/bootloader.c` — 부트 시 vector table의 `SP/PC` 읽기

---

### 관련 문서

- [ISS-OTA-005_no-heartbeat-after-failed-ota.md](ISS-OTA-005_no-heartbeat-after-failed-ota.md) — 같은 빌드 문제에서 파생된 heartbeat 소실 증상
