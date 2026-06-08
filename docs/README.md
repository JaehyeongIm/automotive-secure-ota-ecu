# Secure OTA ECU — 문서 체계 (Documentation Map)

CAN 기반 Dual-ECU **Secure OTA** 프로젝트의 표준 산출물 모음.
요구 → 설계 → 안전/보안 분석 → 시험을 **V-모델**로 잇고, ASPICE(SWE/SEC)·ISO 26262·ISO/SAE 21434 절차에 정합시킨다.
전체 요구 추적은 **[RTM-001](requirements/RTM-001_Requirements_Traceability_Matrix.md)**.

## 폴더 구조

```
docs/
├─ requirements/   요구 명세·추적
├─ design/         아키텍처·상세설계 + adr/(설계결정)
├─ safety/         기능안전 위험분석 (ISO 26262)
├─ security/       사이버보안 위협분석 (ISO/SAE 21434)
├─ test/           시험 계획·결과 (단위·on-target·퍼징)
├─ troubleshooting/ 8D 이슈 기록 (ISS-* / EXP-*)
├─ portfolio/      포트폴리오 산출물
└─ assets/         표준 원문·사진·핀맵 등 자료
```

## 영역별 문서

### 📋 요구 (Requirements) — ASPICE SWE.1
| 문서 | 내용 |
|---|---|
| [SRS-001](requirements/SRS-001_CAN_Secure_OTA_Pipeline.md) | 소프트웨어 요구사항 명세 (기능·보안 SR-ATK/SR-FW·안전 NFR-SAFE) |
| [RTM-001](requirements/RTM-001_Requirements_Traceability_Matrix.md) | 요구↔설계↔코드↔시험 양방향 추적 매트릭스 |

### 🏗 설계 (Design) — ASPICE SWE.2/3
| 문서 | 내용 |
|---|---|
| [SDD-001](design/SDD-001_Secure_OTA_Software_Design.md) | 소프트웨어 아키텍처·상세설계 |
| [diagram](design/diagram.md) | 시스템·시퀀스 다이어그램 |
| [adr/](design/adr/) | 설계 결정 기록 10건 (MADR) — ADR-001~010 |

### 🛡 안전 (Safety) — ISO 26262-3 HARA
| 문서 | 내용 |
|---|---|
| [HARA-001](safety/HARA-001_Hazard_Analysis_Risk_Assessment.md) | 위험원 분석·ASIL·Safety Goal (H-1~H-4) |

### 🔐 보안 (Security) — ISO/SAE 21434 TARA
| 문서 | 내용 |
|---|---|
| [TARA-001](security/TARA-001_Threat_Analysis_Risk_Assessment.md) | 위협 분석·리스크 산정·Cybersecurity Goal (T-1~T-10) |

### 🔬 시험 (Test / Verification) — ASPICE SWE.5/6 · SEC.3
| 문서 | 내용 |
|---|---|
| [TEST_SPEC_OTA](test/TEST_SPEC_OTA_v1.0.md) | 시험 계획·테스트 명세(TS-OTA-001) |
| [SIT-001 Plan](test/SIT-001_System_Integration_Test_Plan.md) | on-target 시스템 통합시험 계획 |
| [SIT-001 RUNBOOK](test/SIT-001_RUNBOOK.md) | 실차 벤치 실행 절차서 |
| [TR-001](test/TR-001_Test_Report.md) | 시험 결과서 (단위 82 + on-target 8/8) |
| [FT-001](test/FT-001_Fuzz_Test_Plan_Report.md) | **퍼즈 테스트 계획·결과** (ASPICE-CS SEC.3) |

### 🧰 트러블슈팅 (8D) — [troubleshooting/](troubleshooting/)
실측 이슈 14건(ISS-*) + 근본원인 실험 1건(EXP-001), 8D 형식.

### 📑 기타
- [portfolio/](portfolio/) — 취업 포트폴리오 산출물
- [assets/](assets/) — Uptane 표준 원문·하드웨어 사진·핀맵·구현계획 등
