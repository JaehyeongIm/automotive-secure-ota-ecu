# ISS-RPI-001: RPi5 SSH 접속 불가

## 개요

| 항목 | 내용 |
|------|------|
| 발생 단계 | Jenkins CI/CD 구축 단계 |
| 발생 시점 | 이틀간 정상 동작 후 갑자기 접속 불가 |
| 증상 | SSH 타임아웃, ping Host is down |
| 상태 | 조사 중 |

---

## 증상

```bash
$ ssh raspberrypi@192.168.45.215
# 응답 없음 (타임아웃)

$ ping 192.168.45.215
Request timeout for icmp_seq 0
ping: sendto: No route to host
ping: sendto: Host is down
# 100% packet loss

$ ping raspberrypi.local
# 공인 IP(218.38.137.27)로 해석됨 → mDNS가 로컬에서 RPi를 찾지 못함
```

---

## 진단 과정

### 1단계: Mac 네트워크 상태 확인

```bash
$ ifconfig | grep "inet 192"
inet 192.168.45.101 netmask 0xffffff00 broadcast 192.168.45.255
```

Mac은 `192.168.45.x` 서브넷에 정상 연결되어 있음 → Mac 네트워크 문제 아님.

### 2단계: ARP 테이블 확인

```bash
$ arp -a
? (192.168.45.215) at (incomplete) on en0 ifscope [ethernet]
```

`(incomplete)` = ARP 요청을 보냈으나 RPi가 응답하지 않음.
IP 주소는 맞지만 RPi의 네트워크 인터페이스가 응답 불가 상태.

### 3단계: 환경 확인

- RPi5 전원: 켜져 있음
- 연결 방식: WiFi
- 모니터 직접 연결: 불가 (RPi5는 micro-HDMI 포트 — 일반 HDMI 케이블 호환 안 됨)

---

## 근본 원인 (추정)

WiFi 연결이 끊긴 후 자동 재연결이 되지 않은 것으로 추정.
RPi는 켜져 있지만 무선 인터페이스(`wlan0`)가 네트워크에서 이탈한 상태.

---

## 해결 방법 체크리스트

### 즉시 시도 가능

- [ ] 전원 케이블 재연결 (완전 재부팅) → 1분 후 SSH 재시도
- [ ] 공유기 관리 페이지(`192.168.45.1`) 접속 → RPi 접속 여부 및 IP 변경 확인
- [ ] 랜 케이블로 공유기에 직접 연결 → WiFi 문제 우회

### 모니터 연결이 필요한 경우

- [ ] micro-HDMI → HDMI 변환 케이블/어댑터 준비
- [ ] 연결 후 `ip addr` 로 인터페이스 상태 확인
- [ ] `sudo ip link set wlan0 up` 으로 인터페이스 복구 시도

---

## 재발 방지

WiFi 연결이 끊겼을 때 자동으로 재연결되도록 RPi에 설정 추가 필요.

```bash
# /etc/wpa_supplicant/wpa_supplicant.conf 또는 NetworkManager 사용 환경에서
# 재연결 스크립트를 cron으로 등록하거나 systemd-networkd 설정 점검
```

장기적으로는 랜 케이블 연결을 권장한다 (WiFi보다 안정적).

---

## 참고

- RPi5 HDMI: micro-HDMI × 2 (일반 HDMI 케이블 직결 불가, 변환 어댑터 필요)
- ARP `(incomplete)` 의미: 해당 IP로 ARP 브로드캐스트를 보냈으나 응답 없음
