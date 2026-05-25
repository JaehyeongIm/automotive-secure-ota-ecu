# Automotive Secure OTA 시스템 다이어그램

---

## 1. Context Diagram (컨텍스트 다이어그램)

시스템과 외부 행위자 간의 관계를 나타냅니다.

```mermaid
graph TD
    DEV["👨‍💻 개발자\n(코드 작성)"]
    GIT["Git Repository\n(GitHub)"]
    RPI["Raspberry Pi 5\nOTA Gateway + Jenkins"]
    DRIVE["DriveECU\n(STM32F446RE)"]
    SENSOR["SensorECU\n(STM32F446RE)"]
    CAN["CAN Bus\n(500kbps)"]

    DEV -->|git push| GIT
    GIT -->|webhook / polling| RPI
    RPI -->|UDS/ISO-TP over CAN| CAN
    CAN -->|펌웨어 전송 0x7E0| DRIVE
    CAN -->|펌웨어 전송 0x7E1| SENSOR
    DRIVE -->|heartbeat 0x100| CAN
    SENSOR -->|heartbeat 0x201| CAN
    CAN -->|슬롯 상태 수신| RPI
```

---

## 2. Block Diagram (블록 다이어그램)

시스템 내부 구성 요소와 연결 구조를 나타냅니다.

```mermaid
graph TD
    subgraph RPi5["Raspberry Pi 5 (OTA Gateway)"]
        JENKINS["Jenkins CI/CD"]
        READ["ci/read_slot.py\n슬롯 상태 읽기"]
        BUILD["ci/build.sh\n펌웨어 빌드\n(arm-none-eabi-gcc)"]
        SIGN["tools/sign_firmware.py\nECDSA-P256 서명"]
        OTA["tools/ota_client.py\nUDS OTA 전송"]
        CANABLE["CANable\n(USB-CAN 어댑터)"]
        JENKINS --> READ
        JENKINS --> BUILD
        BUILD --> SIGN
        SIGN --> OTA
        OTA --> CANABLE
        READ --> CANABLE
    end

    subgraph DriveECU["DriveECU (STM32F446RE)"]
        DBL["Bootloader\n0x08000000\nECDSA 검증"]
        DMETA["Metadata\n0x08008000\nSlot 상태"]
        DSLOTA["Slot A App\n0x08010000\n(192KB)"]
        DSLOTB["Slot B App\n0x08040000\n(256KB)"]
        DTRANS["SN65HVD230\nCAN Transceiver"]
        DBL --> DMETA
        DBL --> DSLOTA
        DBL --> DSLOTB
    end

    subgraph SensorECU["SensorECU (STM32F446RE)"]
        SBL["Bootloader\n0x08000000\nECDSA 검증"]
        SMETA["Metadata\n0x08008000\nSlot 상태"]
        SSLOTA["Slot A App\n0x08010000\n(192KB)"]
        SSLOTB["Slot B App\n0x08040000\n(256KB)"]
        STRANS["SN65HVD230\nCAN Transceiver"]
        SBL --> SMETA
        SBL --> SSLOTA
        SBL --> SSLOTB
    end

    subgraph HW["차량 하드웨어"]
        BTN["B1 버튼\n(USER button, PC13)"]
        MOTOR["TB6612FNG\n모터 드라이버"]
        ULTRA["HC-SR04\n초음파 센서"]
        BTN --> DriveECU
        MOTOR --> DriveECU
        ULTRA --> SensorECU
    end

    CANABLE -->|CANH/CANL| DTRANS
    CANABLE -->|CANH/CANL| STRANS
    DTRANS -->|PA11/PA12| DriveECU
    STRANS -->|PA11/PA12| SensorECU
```

---

## 3. State Diagram (상태 다이어그램)

ECU의 부트로더 및 OTA 세션 상태 전이를 나타냅니다.

```mermaid
stateDiagram-v2
    [*] --> Bootloader : 전원 ON / 재부팅

    state Bootloader {
        [*] --> CheckMetadata : 시작
        CheckMetadata --> ValidateSignature : 메타데이터 유효
        CheckMetadata --> DefaultSlotA : 메타데이터 없음
        ValidateSignature --> JumpToApp : ECDSA 검증 성공
        ValidateSignature --> Waiting : ECDSA 검증 실패
        DefaultSlotA --> JumpToApp
    }

    JumpToApp --> NormalOperation : App 실행

    state NormalOperation {
        [*] --> DRIVE_IDLE : 초기화 완료
        DRIVE_IDLE --> DRIVE_RUNNING : B1 버튼 누름
        DRIVE_RUNNING --> DRIVE_IDLE : 3초 완료 or 장애물 정지 (v1/v2)
        DRIVE_RUNNING --> DRIVE_STOPPED : 10cm 장애물 (v3)
        DRIVE_STOPPED --> DRIVE_REVERSING : 300ms 대기 후 후진 (v3)
        DRIVE_REVERSING --> DRIVE_IDLE : 600ms 후진 완료 (v3)
        DRIVE_IDLE --> DRIVE_IDLE : g_fw_pending 감지\n→ NVIC_SystemReset()
    }

    state OTASession {
        [*] --> ExtendedSession : DiagnosticSessionControl (0x10 0x02)
        ExtendedSession --> SecurityUnlock : SecurityAccess Seed/Key (0x27)
        SecurityUnlock --> Erasing : RequestDownload (0x34)\ng_ota_active=1, Flash Erase ~4초
        Erasing --> Transferring : Erase 완료\ng_ota_active=0
        Transferring --> Transferring : TransferData (0x36) 청크 반복
        Transferring --> TransferDone : RequestTransferExit (0x37)
        TransferDone --> [*] : g_fw_pending=1 세트\n즉시 재부팅 없음
    }

    NormalOperation --> OTASession : UDS 0x10 수신 (주행 중 가능)
    OTASession --> NormalOperation : TransferExit 완료\n(g_fw_pending=1, 주행 재개)
    NormalOperation --> Bootloader : DRIVE_IDLE에서 g_fw_pending 감지\n→ 재부팅 → 슬롯 전환
    Waiting --> [*] : 수동 개입 필요
```

---

## 4. Sequence Diagram (시퀀스 다이어그램)

git push부터 OTA 완료까지 전체 흐름을 나타냅니다.

```mermaid
sequenceDiagram
    participant DEV as 개발자
    participant GIT as Git Repository
    participant JEN as Jenkins (RPi5)
    participant CAN as CAN Bus
    participant ECU as DriveECU

    DEV->>GIT: git push
    GIT->>JEN: webhook 트리거

    JEN->>JEN: git diff로 변경 ECU 감지

    JEN->>CAN: CAN 0x100 수신 대기
    ECU->>CAN: heartbeat [ver, slot=A, ...]
    CAN->>JEN: active_slot = A → target = B

    JEN->>JEN: ci/build.sh drive B\n(SlotB 링커로 빌드)
    JEN->>JEN: sign_firmware.py\n(ECDSA-P256 서명)

    JEN->>CAN: UDS 0x10 0x02 (ExtendedSession)
    CAN->>ECU: DiagnosticSessionControl
    ECU->>CAN: 0x50 0x02 (OK)
    CAN->>JEN: 응답

    JEN->>CAN: UDS 0x27 0x01 (Seed 요청)
    CAN->>ECU: SecurityAccess
    ECU->>CAN: 0x67 0x01 + Seed
    CAN->>JEN: Seed 수신
    JEN->>JEN: Key = Seed XOR 0xDEADBEEF
    JEN->>CAN: UDS 0x27 0x02 + Key
    ECU->>CAN: 0x67 0x02 (Unlock OK)

    JEN->>CAN: UDS 0x34 (RequestDownload)
    ECU->>ECU: g_ota_active=1\n비활성 슬롯(B) Flash Erase (~4초, 모터 정지)
    ECU->>ECU: g_ota_active=0
    ECU->>CAN: 0x74 + maxBlockLen

    Note over ECU,JEN: TransferData 구간: drive_update() 정상 실행 (주행 가능)
    loop 펌웨어 청크 전송 (256 bytes x N)
        JEN->>CAN: UDS 0x36 + chunk
        ECU->>CAN: 0x76 (OK)
    end

    JEN->>CAN: UDS 0x37 (TransferExit)
    ECU->>ECU: ota_meta_write_pending()\n(메타데이터 갱신, active_slot 미변경)
    ECU->>ECU: g_fw_pending = 1
    ECU->>CAN: 0x77 (OK)
    Note over ECU: 즉시 재부팅 없음 — 주행 재개 가능
    CAN->>JEN: 0x77 수신

    Note over ECU: DRIVE_RUNNING / DRIVE_IDLE 상태 정상 유지
    ECU->>CAN: heartbeat [ver, slot=A, driving=0, ...]
    Note over ECU: DRIVE_IDLE 진입 시 g_fw_pending 감지
    ECU->>ECU: NVIC_SystemReset()

    ECU->>ECU: Bootloader: ECDSA 검증
    ECU->>ECU: Slot B App 실행

    JEN->>JEN: 슬롯 전환 대기 (heartbeat 모니터링)
    JEN->>CAN: CAN 0x100 수신 대기
    ECU->>CAN: heartbeat [ver, slot=B, ...]
    CAN->>JEN: 슬롯 전환 확인 (A→B)
    JEN->>JEN: OTA 완료 ✓
```
