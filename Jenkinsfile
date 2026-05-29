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

        // ── 2. DriveECU OTA ────────────────────────────────────────────────────
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
                        python3 tools/ota_client.py \
                            --ecu drive \
                            --channel ${CAN_IF} \
                            --interface socketcan \
                            artifacts/drive_slot${target}_signed.bin
                    """

                    // ECU 재부팅 대기 후 CAN 헤더에서 슬롯 전환 확인
                    sleep 10
                    def newSlot = sh(
                        script: "python3 ci/read_slot.py --ecu drive --channel ${CAN_IF}",
                        returnStdout: true
                    ).trim()
                    if (newSlot != target) {
                        error("[DriveECU] OTA 실패: Slot${target} 기대, Slot${newSlot} 확인됨")
                    }
                    echo "[DriveECU] OTA 완료: Slot${newSlot} 부팅 확인"
                }
            }
        }
        // ── 3. SensorECU OTA ───────────────────────────────────────────────────
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
                        python3 tools/ota_client.py \
                            --ecu sensor \
                            --channel ${CAN_IF} \
                            --interface socketcan \
                            --cf-delay 0.005 \
                            artifacts/sensor_slot${target}_signed.bin
                    """

                    sleep 10
                    def newSlot = sh(
                        script: "python3 ci/read_slot.py --ecu sensor --channel ${CAN_IF}",
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
            // 서명된 바이너리를 Jenkins 빌드 아티팩트로 보관
            archiveArtifacts artifacts: 'artifacts/*.bin', allowEmptyArchive: true
        }
    }
}
