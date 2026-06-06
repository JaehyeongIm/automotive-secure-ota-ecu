# ISS-HW-001: ST-Link "0 KiB flash / Unknown memory region" — 연속 쓰기 중 글리치로 불완전 플래시

> 트러블슈팅(8D 경량판). on-target 테스트 준비(플래시) 중 발견.

| 항목 | 내용 |
|---|---|
| 증상 | `flash.sh`로 한 보드에 연속 st-flash 쓰기 시 3번째(메타 B 0x0800C000)가 `0 KiB flash` / `Unknown memory region`으로 실패 |
| 상태 | ✅ 해결됨 (flash.sh: 쓰기 중 reset 제거 + 재시도) |
| 심각도 | Medium (플래시 차단 + 오진 유발) |
| 영향 | DriveECU 보드 ST-Link 플래시 (Sensor는 정상) |
| 관련 | `tools/flash.sh`, hil_runner `setup_bench`, ISS-CAN-006(같은 세션) |

---

## D2. 문제 정의 (5W2H · Is–Is Not)

- **무엇:** `flash.sh`는 보드당 st-flash를 4회(앱 0x08010000 → 메타 A 0x08008000 → 메타 B 0x0800C000 → reset) 호출하는데, **Drive에서 매번 3번째(메타 B)** 가 실패:
  ```
  STM32F446: 128 KiB SRAM, 0 KiB flash ...
  Unknown memory region
  ```
  `set -e`로 flash.sh가 거기서 중단 → 최종 reset도 안 됨.
- **부작용(중요):** 메타 **사본 A(0x08008000)** 만 새로 써지고 **사본 B(0x0800C000)는 옛값**으로 남음. 부트로더는 *seq 높은 사본*을 고르는데, 옛 B 사본의 seq(0xFFFFFFF8)가 새 A(0xFFFFFFF0)보다 높아 **옛 메타(슬롯B v3, 3-strike 잔류)로 부팅** → "왜 slot0 v2가 아니라 slot1 v3로 뜨지?"라는 오진을 유발.
- **Is:** Drive 보드 + `--reset write`로 쓰기 사이 보드를 실행시킬 때. **Is Not:** Sensor 보드(4회 모두 성공), 두 보드 동시 리셋.

## D4. 근본 원인 분석 (RCA)

- `st-flash --reset write`는 쓰기 후 보드를 **reset → 실행**시킨다. 그러면 도는 앱(CAN IRQ·heartbeat 등)이 다음 st-flash의 **SWD 연결/halt를 방해** → st-flash가 flash 크기를 못 읽어 `0 KiB flash` → 0x0800C000을 매핑 못 함(`Unknown memory region`).
- 메타 이중화에서 **한 사본만 써져** 일관성이 깨졌고, 하필 과거 OTA/3-strike로 진화한 옛 B 사본의 seq가 높아 부트로더가 그걸 선택.
- **근본 원인:** 다중영역 플래시에서 쓰기마다 보드를 reset해 실행 코드와 SWD가 경합. (ST-Link/USB 자체 flakiness도 일부 기여하나, 트리거는 reset-between-writes.)

## D5–D6. 영구 시정 조치 & 검증

- `tools/flash.sh` 수정:
  - 쓰기 사이의 `--reset` 제거 → **쓰는 동안 타깃 halt 유지**(앱이 안 돌아 SWD 간섭 없음), **마지막에 한 번만 reset**.
  - st-flash 1회 **재시도 래퍼** `sf()` 추가(글리치 시 1s 후 1회 재시도).
- 검증: 수정 후 3개 쓰기 모두 `Flash written and verified! jolly good!` + 최종 reset → Drive가 `slot 0 / version v2 OK`로 정상 부팅(옛 B 사본까지 덮임).
- **2차 발현(2026-06-07, hil_runner):** TC-03의 `forge_inject`도 동일 글리치로 메타 B 주입 실패 → 옛 high-seq 메타가 선택돼 fail-closed 미발현(TC-03 FAIL). 같은 패턴 적용: `forge_inject`는 두 사본을 **--reset 없이**(halt 유지) 쓰고 reset은 호출부에서, `stflash()`에 **1회 재시도** 추가. 재실행 시 TC-03 정상.

## D7. 재발 방지 (Lessons Learned)

- **다중영역 플래시는 halt 유지 후 일괄 쓰고 끝에 1회 reset.** 쓰기마다 reset하지 않는다.
- **메타 이중화는 두 사본 모두 성공해야 일관** — 한 사본만 써지면 seq 선택이 옛 사본을 집을 수 있다. 부분 실패 시 abort(set -e)로 노출하되, 재시도로 완주 유도.
- ST-Link `0 KiB flash` 반복 시 USB 재연결 / 보드 전원 재인가.
