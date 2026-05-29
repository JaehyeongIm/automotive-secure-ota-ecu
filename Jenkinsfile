pipeline {
    agent any

    triggers {
        githubPush()
    }

    environment {
        CAN_IF      = 'can0'
        // Jenkins > Manage Credentials에 Secret File로 등록한 ECDSA 개인키.
        // sign_firmware.py는 파일 경로를 인자로 받으므로 Secret File 타입으로 등록해야 한다.
        PRIVATE_KEY = credentials('ota-private-key')
    }

    stages {

        // ── 1. 변경된 ECU 감지 ─────────────────────────────────────────────────
        // git diff로 어떤 ECU 소스가 바뀌었는지 확인한다.
        // DriveECU/ 또는 SensorECU/ 하위 파일이 변경된 경우에만 해당 ECU를 플래시한다.
        stage('Detect changed ECUs') {
            steps {
                script {
                    def changed = sh(
                        script: 'git diff --name-only HEAD~1 HEAD',
                        returnStdout: true
                    ).trim()
                    env.DRIVE_CHANGED  = changed.contains('DriveECU/')  ? 'true' : 'false'
                    env.SENSOR_CHANGED = changed.contains('SensorECU/') ? 'true' : 'false'
                    echo "DriveECU changed:  ${env.DRIVE_CHANGED}"
                    echo "SensorECU changed: ${env.SENSOR_CHANGED}"
                }
            }
        }

        // ── 2. 단위 테스트 ────────────────────────────────────────────────────
        // ceedling test:all 실패 시 이후 모든 스테이지(정적분석·빌드·OTA)를 차단한다.
        stage('Unit Tests') {
            when { expression { env.DRIVE_CHANGED == 'true' || env.SENSOR_CHANGED == 'true' } }
            steps {
                sh 'ceedling test:all'
            }
        }

        // ── 3. 정적 분석 ──────────────────────────────────────────────────────
        // 변경된 ECU의 Core/Src만 검사한다. HAL/CMSIS Drivers/는 생성 코드라 제외.
        // --error-exitcode=1: error 등급 이상 발견 시 파이프라인 중단.
        // --suppress=missingIncludeSystem: Jenkins 환경에 HAL 헤더가 없어 발생하는
        //   include not found 경고를 억제한다 (빌드 결과물이 아닌 정적 분석 한계).
        stage('Static Analysis') {
            when { expression { env.DRIVE_CHANGED == 'true' || env.SENSOR_CHANGED == 'true' } }
            steps {
                script {
                    if (env.DRIVE_CHANGED == 'true') {
                        echo '[cppcheck] DriveECU'
                        sh """
                            cppcheck \
                                --error-exitcode=1 \
                                --suppress=missingIncludeSystem \
                                --inline-suppr \
                                -I DriveECU/Core/Inc \
                                DriveECU/Core/Src
                        """
                    }
                    if (env.SENSOR_CHANGED == 'true') {
                        echo '[cppcheck] SensorECU'
                        sh """
                            cppcheck \
                                --error-exitcode=1 \
                                --suppress=missingIncludeSystem \
                                --inline-suppr \
                                -I SensorECU/Core/Inc \
                                SensorECU/Core/Src
                        """
                    }
                }
            }
        }

        // ── 4. DriveECU OTA ────────────────────────────────────────────────────
        stage('Flash DriveECU') {
            when { expression { env.DRIVE_CHANGED == 'true' } }
            steps {
                script {
                    // CAN 0x100 헤더에서 현재 활성 슬롯 읽기 → 비활성 슬롯이 OTA 대상
                    def active = sh(
                        script: "python3 ci/read_slot.py --ecu drive --channel ${CAN_IF}",
                        returnStdout: true
                    ).trim()
                    def target = (active == 'A') ? 'B' : 'A'
                    echo "[DriveECU] active=Slot${active} → target=Slot${target}"

                    // 대상 슬롯 링커스크립트로 빌드 → artifacts/drive_slot<X>.bin 생성
                    sh "bash ci/build.sh drive ${target}"

                    // ECDSA-P256 서명 추가 → 부트로더 검증 통과용
                    sh """
                        python3 tools/sign_firmware.py \
                            artifacts/drive_slot${target}.bin \
                            ${PRIVATE_KEY} \
                            --out artifacts/drive_slot${target}_signed.bin
                    """

                    // UDS/ISO-TP over CAN (0x7E0→0x7E8) 으로 펌웨어 전송
                    sh """
                        echo '=== [DriveECU] CAN 상태 (UDS 전송 전) ==='
                        ip -details -stats link show ${CAN_IF} || true

                        candump -t a ${CAN_IF} > artifacts/can_dump_drive.log 2>&1 &
                        CANDUMP_PID=\$!
                        echo "candump PID: \$CANDUMP_PID"

                        set +e
                        python3 tools/ota_client.py \
                            --ecu drive \
                            --channel ${CAN_IF} \
                            --interface socketcan \
                            artifacts/drive_slot${target}_signed.bin
                        OTA_EXIT=\$?
                        set -e

                        kill \$CANDUMP_PID 2>/dev/null || true
                        wait \$CANDUMP_PID 2>/dev/null || true

                        echo '=== [DriveECU] CAN 메시지 덤프 (전송 중) ==='
                        cat artifacts/can_dump_drive.log || true

                        echo '=== [DriveECU] CAN 상태 (UDS 전송 후) ==='
                        ip -details -stats link show ${CAN_IF} || true

                        exit \$OTA_EXIT
                    """

                    // 재부팅 완료 후 슬롯 전환 확인 (2-phase: heartbeat 소멸 → 복구)
                    def newSlot = sh(
                        script: "python3 ci/read_slot.py --ecu drive --channel ${CAN_IF} --wait-reboot",
                        returnStdout: true
                    ).trim()
                    if (newSlot != target) {
                        error("[DriveECU] OTA 실패: Slot${target} 기대, Slot${newSlot} 확인됨")
                    }
                    echo "[DriveECU] OTA 완료: Slot${newSlot} 부팅 확인"
                }
            }
        }
        // ── 5. SensorECU OTA ───────────────────────────────────────────────────
        // DriveECU OTA가 성공한 후 순차 실행한다.
        // 두 ECU를 동시에 OTA하지 않는다 (한쪽 ECU가 OTA 중일 때 차량이 멈춘 상태여야 안전).
        stage('Flash SensorECU') {
            when { expression { env.SENSOR_CHANGED == 'true' } }
            steps {
                script {
                    // CAN 0x201 헤더에서 현재 활성 슬롯 읽기
                    def active = sh(
                        script: "python3 ci/read_slot.py --ecu sensor --channel ${CAN_IF}",
                        returnStdout: true
                    ).trim()
                    def target = (active == 'A') ? 'B' : 'A'
                    echo "[SensorECU] active=Slot${active} → target=Slot${target}"

                    sh "bash ci/build.sh sensor ${target}"

                    sh """
                        python3 tools/sign_firmware.py \
                            artifacts/sensor_slot${target}.bin \
                            ${PRIVATE_KEY} \
                            --out artifacts/sensor_slot${target}_signed.bin
                    """

                    // UDS/ISO-TP over CAN (0x7E1→0x7E9) 으로 펌웨어 전송
                    sh """
                        echo '=== [SensorECU] CAN 상태 (UDS 전송 전) ==='
                        ip -details -stats link show ${CAN_IF} || true

                        candump -t a ${CAN_IF} > artifacts/can_dump_sensor.log 2>&1 &
                        CANDUMP_PID=\$!
                        echo "candump PID: \$CANDUMP_PID"

                        set +e
                        python3 tools/ota_client.py \
                            --ecu sensor \
                            --channel ${CAN_IF} \
                            --interface socketcan \
                            --cf-delay 0.005 \
                            artifacts/sensor_slot${target}_signed.bin
                        OTA_EXIT=\$?
                        set -e

                        kill \$CANDUMP_PID 2>/dev/null || true
                        wait \$CANDUMP_PID 2>/dev/null || true

                        echo '=== [SensorECU] CAN 메시지 덤프 (전송 중) ==='
                        cat artifacts/can_dump_sensor.log || true

                        echo '=== [SensorECU] CAN 상태 (UDS 전송 후) ==='
                        ip -details -stats link show ${CAN_IF} || true

                        exit \$OTA_EXIT
                    """

                    def newSlot = sh(
                        script: "python3 ci/read_slot.py --ecu sensor --channel ${CAN_IF} --wait-reboot",
                        returnStdout: true
                    ).trim()
                    if (newSlot != target) {
                        error("[SensorECU] OTA 실패: Slot${target} 기대, Slot${newSlot} 확인됨")
                    }
                    echo "[SensorECU] OTA 완료: Slot${newSlot} 부팅 확인"
                }
            }
        }
    }

    post {
        success {
            echo 'OTA 파이프라인 완료'
        }
        failure {
            echo '파이프라인 실패 — ECU는 이전 펌웨어 유지'
        }
        always {
            // 서명된 바이너리 및 CAN 덤프 로그를 Jenkins 빌드 아티팩트로 보관
            archiveArtifacts artifacts: 'artifacts/*.bin, artifacts/can_dump_*.log', allowEmptyArchive: true
        }
    }
}
