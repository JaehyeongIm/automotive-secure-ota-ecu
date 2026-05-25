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
        IR["IR 라인센서\n4채널"]
        MOTOR["TB6612FNG\n모터 드라이버"]
        ULTRA["HC-SR04\n초음파 센서"]
        IR --> DriveECU
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
        [*] --> Driving : 주행 중
        Driving --> ObstacleStop : 장애물 감지
        ObstacleStop --> Driving : 장애물 해제
        Driving --> OTASession : UDS 0x10 수신
        ObstacleStop --> OTASession : UDS 0x10 수신
    }

    state OTASession {
        [*] --> ExtendedSession : DiagnosticSessionControl
        ExtendedSession --> SecurityUnlock : SecurityAccess Seed/Key
        SecurityUnlock --> Downloading : RequestDownload
        Downloading --> Transferring : TransferData (청크 반복)
        Transferring --> Transferring : 다음 청크
        Transferring --> TransferDone : RequestTransferExit
        TransferDone --> [*] : 재부팅
    }

    OTASession --> Bootloader : 재부팅 후 슬롯 전환
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
    CAN->>ECU: 비활성 슬롯(B) 플래시 준비\n(섹터 Erase ~4초)
    ECU->>CAN: 0x74 + maxBlockLen

    loop 펌웨어 청크 전송 (256 bytes x N)
        JEN->>CAN: UDS 0x36 + chunk
        ECU->>CAN: 0x76 (OK)
    end

    JEN->>CAN: UDS 0x37 (TransferExit)
    ECU->>CAN: 0x77 (OK)
    ECU->>ECU: 메타데이터 업데이트\n(active_slot = B)
    ECU->>ECU: 재부팅

    ECU->>ECU: Bootloader: ECDSA 검증
    ECU->>ECU: Slot B App 실행

    JEN->>JEN: 10초 대기
    JEN->>CAN: CAN 0x100 수신 대기
    ECU->>CAN: heartbeat [ver, slot=B, ...]
    CAN->>JEN: 슬롯 전환 확인 (A→B)
    JEN->>JEN: OTA 완료 ✓
```
