# ISS-XXX-NNN: <제목 한 줄>

> 트러블슈팅 문서 템플릿 — 자동차 실무의 **8D(Eight Disciplines)** 경량판.
> 새 문서는 이 구조를 따른다. 해당 없는 D는 생략 가능. (참고: ASPICE SUP.9 Problem Resolution)

| 항목 | 내용 |
|---|---|
| 증상 | <현장에서 검색할 한 줄 증상> |
| 상태 | 진행 중 / ✅ 해결됨 |
| 심각도 | High / Medium / Low |
| 영향 | <ECU · 기능 · 범위> |
| 관련 | FR-… / TC-… / ADR-… (추적성) |

---

## D2. 문제 정의 (5W2H · Is–Is Not)

- 무엇이 / 어디서 / 언제 / 얼마나 — 정량적으로
- **Is**: 문제가 나타나는 조건 ↔ **Is Not**: 정상인 조건 (원인 범위를 좁힌다)
- 재현 절차

## D3. 임시 조치 (Containment) — *해당 시*

영구 수정 전에 당장 영향을 막은 조치.

## D4. 근본 원인 분석 (RCA)

- 사용한 기법 명시: **5 Whys** / **Ishikawa(피시본, 6M: Man·Machine·Material·Method·Measurement·Environment)**
- 진단 과정 + **배제한 가설(Is Not 근거)** + 최종 근본 원인

## D5–D6. 영구 시정 조치 & 검증

- 수정 내용 (코드/설정)
- 검증: D2의 재현 절차로 증상이 해소됨을 확인

## D7. 재발 방지 (Lessons Learned)

- 같은 클래스의 결함을 막을 시스템적 대책 (체크리스트 · FMEA 갱신 · 정적분석 룰 등)
- 교훈
