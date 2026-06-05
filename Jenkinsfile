pipeline {
    agent any

    triggers {
        githubPush()
    }

    environment {
        CAN_IF      = 'can0'
        // Jenkins > Manage Credentials에 Secret File로 등록한 ECDSA 개인키.
        // 'Build & Sign Release'(릴리스 태그) 단계에서만 사용한다 — 배포 단계는 개인키 불필요.
        PRIVATE_KEY = credentials('ota-private-key')
    }

    stages {

        // ── 1. 변경 ECU + 릴리스 태그 감지 (ADR-008) ───────────────────────────
        // CI(브랜치 push)와 배포(릴리스 태그)를 분리한다.
        //  - 일반 push: 단위테스트·정적분석·컴파일 검증까지만, ECU 미접촉.
        //  - 태그 vN  : 빌드·서명·승인 후에만 ECU에 배포. N은 anti-rollback 버전(ADR-007).
        stage('Detect changes & release') {
            steps {
                script {
                    // HEAD가 정확히 vN 태그이면 릴리스. 태그 숫자가 펌웨어 version이 된다.
                    // (전제: SCM 체크아웃이 태그를 포함해야 한다 — multibranch면 자동.)
                    def tag = sh(
                        script: 'git describe --tags --exact-match HEAD 2>/dev/null || true',
                        returnStdout: true
                    ).trim()
                    env.IS_RELEASE      = (tag ==~ /v\d+/) ? 'true' : 'false'
                    env.RELEASE_VERSION = (tag ==~ /v\d+/) ? tag.replaceFirst('v', '') : ''

                    // 변경 ECU 감지. 릴리스면 직전 태그와, 아니면 직전 커밋과 비교한다.
                    // 단, 첫 릴리스는 직전 태그가 없어 증분 diff 기준선이 없다 → 전체 ECU 배포(deploy-all).
                    def base = 'HEAD~1'
                    def firstRelease = false
                    if (env.IS_RELEASE == 'true') {
                        def prev = sh(
                            script: "git describe --tags --abbrev=0 ${tag}^ 2>/dev/null || true",
                            returnStdout: true
                        ).trim()
                        if (prev) { base = prev } else { firstRelease = true }
                    }
                    if (firstRelease) {
                        // 첫 릴리스: 비교 기준선이 없으므로 두 ECU 모두 배포한다(full release).
                        env.DRIVE_CHANGED  = 'true'
                        env.SENSOR_CHANGED = 'true'
                    } else {
                        def changed = sh(
                            script: "git diff --name-only ${base} HEAD",
                            returnStdout: true
                        ).trim()
                        env.DRIVE_CHANGED  = changed.contains('DriveECU/')  ? 'true' : 'false'
                        env.SENSOR_CHANGED = changed.contains('SensorECU/') ? 'true' : 'false'
                    }

                    echo "release=${env.IS_RELEASE} version=${env.RELEASE_VERSION} " +
                         "(base=${firstRelease ? 'first-release→deploy-all' : base})  " +
                         "drive=${env.DRIVE_CHANGED} sensor=${env.SENSOR_CHANGED}"
                }
            }
        }

        // ── 2. 단위 테스트 ────────────────────────────────────────────────────
        // ceedling test:all 실패 시 이후 모든 스테이지(정적분석·빌드·배포)를 차단한다.
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

        // ── 4. 컴파일 검증 (CI, ECU 미접촉) ───────────────────────────────────
        // 일반 push에서 크로스컴파일이 깨지지 않는지만 확인한다(FR-CICD-003).
        // 서명·ECU 전송은 하지 않는다. 슬롯은 검증용으로 B 하나만 빌드.
        stage('Compile Check') {
            when {
                allOf {
                    expression { env.IS_RELEASE == 'false' }
                    expression { env.DRIVE_CHANGED == 'true' || env.SENSOR_CHANGED == 'true' }
                }
            }
            steps {
                script {
                    if (env.DRIVE_CHANGED == 'true')  { sh 'bash ci/build.sh drive B' }
                    if (env.SENSOR_CHANGED == 'true') { sh 'bash ci/build.sh sensor B' }
                }
            }
        }

        // ── 5. 릴리스 빌드 + 서명 (태그 vN, Uptane Image Repository) ───────────
        // build-all/deploy-select: A·B 두 슬롯 변형을 모두 빌드·서명·보관하고,
        // 배포 단계는 live 슬롯을 읽어 재빌드 없이 해당 아티팩트를 고른다.
        // 개인키는 이 단계에서만 사용한다.
        stage('Build & Sign Release') {
            when { expression { env.IS_RELEASE == 'true' } }
            steps {
                script {
                    def signEcu = { ecu, id ->
                        for (slot in ['A', 'B']) {
                            sh "bash ci/build.sh ${ecu} ${slot}"
                            sh """
                                python3 tools/sign_firmware.py \
                                    artifacts/${ecu}_slot${slot}.bin \
                                    ${PRIVATE_KEY} \
                                    --version ${env.RELEASE_VERSION} \
                                    --ecu-id ${id} \
                                    --out artifacts/${ecu}_slot${slot}_signed.bin
                            """
                        }
                    }
                    if (env.DRIVE_CHANGED  == 'true') { signEcu('drive', 1) }
                    if (env.SENSOR_CHANGED == 'true') { signEcu('sensor', 2) }
                }
            }
        }

        // ── 6. 배포 승인 게이트 (UN R156 SUMS 승인의 미니어처) ─────────────────
        // 태그 = 배포 자격, 승인 = 지금 이 ECU에 꽂을 권한. 승인자를 기록한다.
        // 주의: 승인 대기 동안 단일 노드 executor를 점유한다(데모 허용, ADR-008 §4).
        stage('Approve Deployment') {
            when {
                allOf {
                    expression { env.IS_RELEASE == 'true' }
                    expression { env.DRIVE_CHANGED == 'true' || env.SENSOR_CHANGED == 'true' }
                }
            }
            steps {
                timeout(time: 30, unit: 'MINUTES') {
                    script {
                        env.APPROVER = input(
                            message: "릴리스 v${env.RELEASE_VERSION} 배포 승인 " +
                                     "(drive=${env.DRIVE_CHANGED}, sensor=${env.SENSOR_CHANGED})",
                            ok: '배포 승인',
                            submitterParameter: 'APPROVER'
                        )
                        echo "[DEPLOY] 승인자=${env.APPROVER}  release=v${env.RELEASE_VERSION}"
                    }
                }
            }
        }

        // ── 7. DriveECU 배포 ──────────────────────────────────────────────────
        // 사전 서명된 아티팩트를 선택해 전송한다(재빌드·재서명 없음, 개인키 불필요).
        stage('Deploy DriveECU') {
            when {
                allOf {
                    expression { env.IS_RELEASE == 'true' }
                    expression { env.DRIVE_CHANGED == 'true' }
                }
            }
            steps {
                script {
                    // CAN 0x100 헤더에서 현재 활성 슬롯 읽기 → 비활성 슬롯이 OTA 대상
                    def active = sh(
                        script: "python3 ci/read_slot.py --ecu drive --channel ${CAN_IF}",
                        returnStdout: true
                    ).trim()
                    def target = (active == 'A') ? 'B' : 'A'
                    echo "[DriveECU] active=Slot${active} → target=Slot${target} (v${env.RELEASE_VERSION})"

                    // UDS/ISO-TP over CAN (0x7E0→0x7E8) 으로 사전 서명 펌웨어 전송
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

        // ── 8. SensorECU 배포 ─────────────────────────────────────────────────
        // DriveECU 배포가 성공한 후 순차 실행한다.
        // 두 ECU를 동시에 OTA하지 않는다 (한쪽 ECU가 OTA 중일 때 차량이 멈춘 상태여야 안전).
        stage('Deploy SensorECU') {
            when {
                allOf {
                    expression { env.IS_RELEASE == 'true' }
                    expression { env.SENSOR_CHANGED == 'true' }
                }
            }
            steps {
                script {
                    // CAN 0x201 헤더에서 현재 활성 슬롯 읽기
                    def active = sh(
                        script: "python3 ci/read_slot.py --ecu sensor --channel ${CAN_IF}",
                        returnStdout: true
                    ).trim()
                    def target = (active == 'A') ? 'B' : 'A'
                    echo "[SensorECU] active=Slot${active} → target=Slot${target} (v${env.RELEASE_VERSION})"

                    // UDS/ISO-TP over CAN (0x7E1→0x7E9) 으로 사전 서명 펌웨어 전송
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
            script {
                if (env.IS_RELEASE == 'true') {
                    echo "릴리스 v${env.RELEASE_VERSION} 배포 파이프라인 완료"
                } else {
                    echo 'CI 통과 — 배포하려면 vN 태그를 push 하세요 (ADR-008)'
                }
            }
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
