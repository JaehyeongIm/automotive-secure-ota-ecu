# ISS-SEC-002: SecurityAccess NRC 0x35 — 게이트웨이(Pi) 구버전 ota_client로 Key 불일치

> 트러블슈팅(8D 경량판). on-target OTA 테스트 중 발견.

| 항목 | 내용 |
|---|---|
| 증상 | OTA 푸시 시 seed는 양쪽 일치하는데 Key가 달라 `NRC SID=0x27 code=0x35`(invalidKey), unlock 실패 |
| 상태 | ✅ 해결됨 (Pi `git pull`로 ota_client 최신화) |
| 심각도 | Medium (OTA 테스트 차단) |
| 영향 | RPi 게이트웨이 `ota_client.py` ↔ ECU SecurityAccess(0x27) |
| 관련 | FR-CAN-010, ADR-004(seed/HMAC 정합), RFC 2104, ISO 14229 0x27 |

---

## D2. 문제 정의 (5W2H · Is–Is Not)

- **무엇:** Pi에서 OTA 푸시 → Extended 세션·seed 요청까지 정상인데 sendKey에서 거부:
  ```
  [UDS]   Seed = 0x31C7615D     (ECU UART도 동일)
  [UDS]   Key  = 0xEF6ADFB2     (Pi가 보낸 값)
  [OTA] ERROR: NRC SID=0x27 code=0x35
  ```
- **Is:** Pi 레포가 옛 커밋(bf5ce13)일 때. **Is Not:** Pi 최신 ota_client(unlock OK).

## D4. 근본 원인 분석 (RCA — 가설 제거식)

`Key = HMAC-SHA256(PSK, Seed)[:4]`. seed가 같은데 key가 다르면 **PSK 또는 유도식**이 다른 것. 다음 순서로 좁혔다:

1. **칩 PSK 직접 read** — `st-flash read 0x08007FE0 32` → `OTA-DEV-PSK-DO-NOT-USE-IN-PROD!!`(dev). 즉 ECU는 dev PSK.
2. **Pi PSK 확인** — `echo $OTA_PSK_HEX` 빈 문자열 → Pi도 dev여야 함.
3. **HMAC 직접 계산(결정적 증거)** — `HMAC(dev, 0x31C7615D)[:4] = 0xED52AEB8`. 그런데 Pi가 보낸 건 `0xEF6ADFB2`. → **Pi는 dev PSK/현재 유도식으로 계산하지 않았다.**
4. **Pi 레포 상태** — `git log` 결과 옛 커밋. Pi의 `ota_client.py`가 구버전(옛 `_DEV_PSK` 또는 옛 key 유도식).
- **근본 원인:** **벤치 노드(Pi) 코드 미동기화** — 구버전 ota_client가 현재 ECU 펌웨어의 key 유도식과 불일치. (ECU·PSK·CAN은 모두 정상이었음.)

> 진단 전환점: "Pi가 맞나 ECU가 맞나"를 *칩 PSK read + HMAC 자가계산*으로 갈랐다. 호스트 로그만으로는 "key 틀림"까지만 보였다(ISS-OTA-004의 교훈과 동일 — 구분 가능한 증거를 먼저 확보).

## D5–D6. 영구 시정 조치 & 검증

- Pi에서 `git pull`(빌드 산출물 로컬변경은 `git checkout --`로 버리고) → `tools/ota_client.py` 최신화(현재 유도식 `HMAC(PSK,Seed)[:4]` + dev `_DEV_PSK`).
- 검증: 재푸시 시 `Seed=0xF64A7E51 → Key=0x8315025F → [UDS] Unlocked` → RequestDownload 진입 성공.

## D7. 재발 방지 (Lessons Learned)

- **OTA 전 체크리스트에 "Pi 레포 == ECU 펌웨어 커밋" 동기화 확인** 추가(`git rev-parse HEAD` 대조).
- **PSK 정책 명시** — dev placeholder vs 운영 `OTA_PSK_HEX`. 운영은 `OTA_PSK_HEX`를 양쪽(부트로더 프로비저닝 + Pi) 동일하게.
- **key 디버깅 표준 절차:** ① 칩 PSK read(0x08007FE0) ② `HMAC(PSK,seed)[:4]` 자가계산 ③ 양측 산출 key 비교 → 어느 노드가 틀린지 즉시 판별.
- 표준: ISO 14229 SecurityAccess(0x27), RFC 2104 HMAC, ADR-004.
