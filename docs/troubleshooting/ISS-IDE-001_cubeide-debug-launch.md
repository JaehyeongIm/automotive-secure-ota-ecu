# ISS-IDE-001: macOS CubeIDE 디버그 실행 불가

## 개요

| 항목 | 내용 |
|------|------|
| 발생 환경 | macOS (Apple Silicon), STM32CubeIDE |
| 증상 | Debug/Run 시 `arm-none-eabi-gdb` 실행 실패, "Unsupported build configuration" |
| 상태 | ✅ 해결됨 |

---

## D2. 문제 정의 — 증상

CubeIDE에서 Debug 또는 Run 버튼을 누르면 두 가지 에러가 발생한다.

**에러 1 — GDB 실행 실패**
```
Error with command: arm-none-eabi-gdb --version
Cannot run program "arm-none-eabi-gdb": Unknown reason
```

**에러 2 — 빌드 구성 인식 실패**
```
Unsupported build configuration. MCU ARM GCC required for debug.
```

UART 출력 및 Jenkins 파이프라인 빌드는 정상 동작하므로 소스코드나 펌웨어 자체 문제는 아님.

---

## D4. 근본 원인 분석 — 진단 과정

### 1단계 — GDB 바이너리 존재 여부 확인

```bash
find /Applications/STM32CubeIDE.app -name "arm-none-eabi-gdb"
```

바이너리가 아래 경로에 존재함을 확인:
```
/Applications/STM32CubeIDE.app/Contents/Eclipse/plugins/
com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.14.3.rel1.macosaarch64_1.0.0.202602081740/
tools/bin/arm-none-eabi-gdb
```

터미널에서 직접 실행하면 정상 동작:
```bash
"/Applications/STM32CubeIDE.app/.../arm-none-eabi-gdb" --version
# GNU gdb (GNU Tools for STM32 14.3.rel1) 15.2.90 정상 출력
```

→ 바이너리 자체는 정상. CubeIDE GUI에서만 실행 실패.

### 2단계 — 확장 속성 확인

```bash
ls -la ".../arm-none-eabi-gdb"
# -rwxr-xr-x@  ← @ 가 확장 속성 존재를 의미

xattr -l ".../arm-none-eabi-gdb"
# com.apple.quarantine: 03c1;69ccb4c3;Chrome;AC2BE8E3-...
```

`com.apple.quarantine` 플래그 발견.
Chrome으로 CubeIDE 설치 파일을 다운로드할 때 macOS가 자동으로 붙인 격리 플래그.
터미널은 이 플래그를 우회하지만 **GUI 앱은 플래그가 있는 바이너리 실행을 차단**한다.

### 3단계 — .cproject 존재 여부 확인

```bash
ls -la DriveECU/ | grep cproject
# 출력 없음 → .cproject 파일이 없음
```

`.gitignore`를 확인하니 `.cproject`가 명시적으로 제외되어 있었다:
```
# STM32CubeIDE / STM32CubeMX
.cproject
.project
.settings/
```

git 히스토리에서 삭제 커밋 확인:
```bash
git log --all --oneline --diff-filter=D -- "DriveECU/.cproject"
# f151e42 chore: IDE 설정 파일 gitignore 추가 및 추적 제거
```

---

## D4. 근본 원인 (2가지)

### 원인 1 — macOS quarantine 플래그

CubeIDE를 Chrome으로 다운로드하면 설치 파일에 `com.apple.quarantine`이 붙는다.
앱 번들 전체가 아닌 내부 바이너리들에도 플래그가 전파되어, CubeIDE가 내장 툴(`arm-none-eabi-gdb` 등)을 실행하려 할 때 macOS Gatekeeper가 차단한다.

터미널에서는 실행되지만 GUI 앱에서는 차단되는 이유:
터미널 실행은 사용자 세션의 권한을 상속받아 우회되는 반면, GUI 앱 내부에서 child process를 spawn할 때는 더 엄격한 검사가 적용된다.

### 원인 2 — .cproject 유실

commit `f151e42`에서 `.cproject`를 git 추적에서 제거하고 gitignore에 추가했다.

이 커밋 이전 브랜치에는 `.cproject`가 추적된 파일로 존재했고, 이후 브랜치로 `git checkout` 시 git이 추적 대상이 아닌 파일을 작업 디렉토리에서 제거했다.

**gitignore의 함정:** "gitignore된 파일은 checkout에 안전하다"는 규칙은 *처음부터 추적된 적 없는 파일*에만 해당된다. 한번이라도 커밋된 뒤 `git rm --cached`로 추적 제거된 파일은, 해당 파일이 있는 브랜치 ↔ 없는 브랜치 전환 시 삭제된다.

`.cproject`가 없으면 CubeIDE가 프로젝트를 MCU ARM GCC 프로젝트로 인식하지 못해 "Unsupported build configuration" 에러가 발생한다.

---

## Jenkins 파이프라인이 영향받지 않은 이유

Jenkins는 `.cproject`를 사용하지 않는다. `ci/build.sh`가 CubeIDE가 한번 생성해둔 `Debug/makefile`을 직접 실행하기 때문이다.

```
.cproject → (CubeIDE 코드 생성 시 1회) → Debug/makefile  ← Jenkins가 사용
```

`Debug/makefile`은 git에 커밋되어 있으므로 `.cproject` 유실과 무관하게 빌드된다.
`.cproject`는 로컬 CubeIDE GUI 전용 설정 파일이다.

---

## D5–D6. 시정 조치 & 검증

### 1 — quarantine 플래그 제거

```bash
sudo xattr -cr /Applications/STM32CubeIDE.app
```

앱 번들 전체에서 재귀적으로 모든 확장 속성을 제거한다.
실행 후 CubeIDE 재시작 필요.

### 2 — .cproject 재생성

CubeIDE에서 `DriveECU/DriveECU.ioc` 더블클릭 → **Project → Generate Code** (`Alt+K`).

생성 직후 확인 필요한 설정 (Generate Code는 커스텀 설정을 초기값으로 되돌릴 수 있음):

| 항목 | 위치 | Slot A 기준 값 |
|------|------|----------------|
| 링커 스크립트 | Project Properties → C/C++ Build → Settings → Linker | `STM32F446RETX_FLASH.ld` |
| VECT_TAB_OFFSET | `Core/Src/system_stm32f4xx.c` | `0x00010000U` |

`main.c`의 `USER CODE` 블록 안 코드는 Generate Code 후에도 보존된다.

---

## D7. 재발 방지

**quarantine 재발생 방지:** CubeIDE 업데이트 시에도 동일하게 발생할 수 있다. 업데이트 후 Debug가 안 되면 동일한 `xattr -cr` 명령을 실행한다.

**브랜치 전환 후 .cproject 유실:** `git checkout` 후 CubeIDE Debug가 안 되면 `.ioc`로 재생성한다. `.cproject`는 로컬 전용이므로 커밋할 필요 없다.

---

## 관련 파일

| 파일 | 역할 |
|------|------|
| `DriveECU/.cproject` | CubeIDE 빌드 구성 (gitignore, 로컬 전용) |
| `DriveECU/DriveECU.ioc` | CubeMX 하드웨어 설정 — .cproject 재생성 원본 |
| `DriveECU/Debug/makefile` | Jenkins CI 빌드용 Makefile (git 추적됨) |
| `ci/build.sh` | Jenkins 빌드 스크립트 — make 직접 호출 |
