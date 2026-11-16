# eBPF execmon 실습 서버 생성·설치 가이드

작성일: 2026-10-28  
대상: execmon을 빌드·실행할 **전용 Linux 실습 VM**  
범위: Kubernetes, 운영 배포, 자동 차단은 포함하지 않는다.

## 1. 목표

이 서버는 아래 학습 흐름을 실행하는 단기 실습 환경이다.

~~~text
개발 PC
  → SSH 키 인증
  → 전용 eBPF 실습 VM
  → execmon 빌드·load·attach·테스트
~~~

eBPF 프로그램은 커널 안에서 실행되므로, 기존 운영 서버나 업무용 서버가 아닌 별도 VM을 사용한다.

## 1.1 전체 설정 순서

서버를 만든 뒤 아래 순서로 진행한다. 아직 eBPF 코드를 실행하는 단계가 아니므로, 각 단계의 확인 명령 결과를 기록하는 데 집중한다.

```text
VM 생성과 파티션 구성
  → 초기 sudo 계정 접속
  → 기본 OS 설정·업데이트
  → SSH 키·전용 계정 설정
  → 방화벽과 SSH 하드닝
  → eBPF 개발 패키지 설치
  → BTF·helper·tracepoint 기능 확인
  → execmon 소스 배치
```

## 2. 권장 사양

| 항목 | 최소로 가능한 수준 | 이번 실습 권장값 | 이유 |
| --- | ---: | ---: | --- |
| vCPU | 1 | **2 vCPU** | clang 빌드와 execmon·테스트 명령을 함께 실행할 여유 |
| RAM | 2 GiB | **4 GiB** | Ubuntu, clang/LLVM, libbpf, 로그 관찰을 무리 없이 실행 |
| 디스크 | 20 GiB | **30 GiB SSD** | OS 업데이트, headers, LLVM, 소스·빌드 산출물·로그 공간 |
| Swap | 없어도 가능 | 1~2 GiB 선택 | 작은 VM에서 패키지 설치·컴파일 중 메모리 부족 완화 |
| 네트워크 | SSH만 필요 | 공인 IP 또는 bastion 경유 | 인바운드는 관리자 SSH만 허용 |

**권장 생성값: 2 vCPU / 4 GiB RAM / 30 GiB SSD / Ubuntu Server 24.04 LTS / x86_64**

현재 소스는 ring buffer와 bpf_get_current_task_btf() helper를 사용한다. 따라서 최소 커널 기준은 **Linux 5.11 이상**이다. Ubuntu 24.04 LTS는 이 조건보다 새 커널 계열이며 2029년 5월까지 표준 보안 유지보수 대상이다. [Ubuntu 24.04 LTS 릴리스 정보](https://documentation.ubuntu.com/release-notes/24.04/)

> ARM64도 가능하지만 BPF 컴파일 옵션을 -D__TARGET_ARCH_arm64로 바꿔야 한다. 첫 실습은 문서와 명령이 단순한 x86_64를 권장한다.

## 2.1 권장 파티션 구성

30 GiB는 작지만 이 학습 프로젝트에는 충분하다. 다만 패키지 캐시, kernel header, LLVM, 소스와 이벤트 로그가 한 파일시스템을 가득 채우면 원인 파악이 어려워진다. 따라서 시스템 영역과 학습 파일 영역을 분리한다.

### 추천: LVM 기반 분리 구성

LVM(Logical Volume Manager)은 실제 디스크 공간을 논리 볼륨으로 나눠, 나중에 여유 공간을 필요한 볼륨에 더 배정하기 쉽게 하는 Linux 저장소 관리 방식이다.

| 장치 또는 볼륨 | 크기 | 마운트 위치 | 용도 |
| --- | ---: | --- | --- |
| EFI System Partition | 512 MiB | /boot/efi | UEFI 부팅용. BIOS VM이면 만들지 않는다. |
| 일반 파티션 | 1 GiB | /boot | kernel과 부트로더 파일 |
| LVM logical volume: root | 18 GiB | / | 운영체제와 패키지 |
| LVM logical volume: home | 7 GiB | /home | ebpf-lab 계정, 소스, build 결과, 실습 로그 |
| LVM logical volume: swap | 2 GiB | swap | 일시적인 메모리 여유 |
| LVM volume group의 미할당 공간 | 약 1 GiB 이상 | 없음 | / 또는 /home 확장용 여유 |

파일시스템은 ext4를 사용한다. 실제 설치 화면의 단위와 LVM 메타데이터 때문에 정확히 표와 같은 합계가 아닐 수 있으므로, 마지막 1 GiB 이상은 할당하지 않고 남겨 둔다.

### 왜 /home을 나누는가?

이 프로젝트의 소스, build 산출물, 테스트 출력 파일은 `/home/ebpf-lab/workspace`에 둔다. 부하 테스트 로그가 예상보다 커져도 `/` 전체가 바로 가득 차는 위험을 줄일 수 있다.

단, `/home`에 `noexec` 마운트 옵션을 주지 않는다. 이 실습은 `/home/ebpf-lab/workspace/execmon/build/execmon` 바이너리를 실행해야 하기 때문이다.

### Ubuntu Server 설치 화면에서의 선택 순서

1. Storage configuration에서 **Custom storage layout**을 선택한다.
2. 디스크 파티션 테이블은 GPT를 선택한다.
3. UEFI VM이면 512 MiB FAT32 EFI System Partition을 만들고 `/boot/efi`에 마운트한다.
4. 1 GiB ext4 파티션을 만들고 `/boot`에 마운트한다.
5. 남은 공간으로 LVM volume group(예: `vg_ebpf`)을 만든다.
6. 그 안에 `lv_root` 18 GiB, `lv_home` 7 GiB, `lv_swap` 2 GiB를 만든다.
7. `lv_root`는 ext4 `/`, `lv_home`은 ext4 `/home`, `lv_swap`은 swap으로 설정한다.
8. volume group의 나머지 공간은 할당하지 않은 채 남긴다.
9. 최종 요약에서 **대상 디스크가 새 실습 VM의 디스크인지** 확인한 뒤 설치를 확정한다.

> 이 절차는 새 VM의 빈 디스크에만 적용한다. 이미 데이터가 있는 디스크에서 파티션을 변경하지 않는다.

### 단순 구성을 선택해도 되는 경우

LVM이 처음이라면 설치기의 **Use an entire disk with LVM** 옵션으로 먼저 설치해도 된다. 이 경우 `/home` 분리 여부와 남은 LVM 공간을 설치 요약에서 확인한다. 파티션을 수동으로 나누는 것보다, 별도 실습 VM과 스냅샷을 갖추는 편이 더 중요하다.

### 설치 후 확인

```bash
lsblk -o NAME,SIZE,FSTYPE,TYPE,MOUNTPOINTS
findmnt -no SOURCE,FSTYPE,SIZE,AVAIL /
findmnt -no SOURCE,FSTYPE,SIZE,AVAIL /home
swapon --show
df -hT
sudo vgs
sudo lvs
```

예상 결과:

- `/`와 `/home`이 별도 ext4 파일시스템으로 보인다.
- swap이 활성 상태다.
- `vgs` 결과에 확장에 쓸 여유 공간(VFree)이 남아 있다.

## 3. 서버 생성 시 선택 사항

클라우드, VMware, VirtualBox 등 환경과 관계없이 아래 기준을 적용한다.

- 이미지: **Ubuntu Server 24.04 LTS 64-bit**
- 서버 이름 예시: ebpf-lab-01
- 위치: 업무 운영망이 아닌 실습용 네트워크 또는 별도 프로젝트/VPC
- 스냅샷: 패키지 설치 전 하나, 기능 점검 성공 뒤 하나
- 인바운드: TCP 22만, **본인의 현재 공인 IP** 또는 bastion에서만 허용
- 인바운드에 0.0.0.0/0을 쓰지 않는다.
- 초기 계정은 아래 단계에서 ebpf-lab 전용 계정으로 분리한다.
- root 로그인·비밀번호 로그인은 SSH 키 검증이 끝난 뒤 비활성화한다.

서버 비밀번호, SSH private key, 클라우드 access key는 채팅·Git 저장소·프로젝트 문서에 기록하지 않는다.

## 3.1 초기 OS 설정과 보안 업데이트

이 단계는 VM의 **초기 sudo 가능 계정**으로 첫 로그인한 직후 수행한다. 클라우드 이미지의 기본 계정 이름은 제공자마다 다를 수 있으므로, 아래 명령에서 현재 로그인한 계정을 확인한다.

```bash
whoami
hostnamectl
timedatectl status
df -h /
free -h
```

### 호스트 이름과 시간대

서버 이름은 실습 용도가 드러나게 정한다. 한국 시간대로 기록을 남기려면 다음처럼 설정한다.

```bash
sudo hostnamectl set-hostname ebpf-lab-01
sudo timedatectl set-timezone Asia/Seoul
sudo timedatectl set-ntp true
hostnamectl
timedatectl status
```

예상 결과:

- Static hostname이 `ebpf-lab-01`
- Time zone이 `Asia/Seoul (KST, +0900)`
- NTP service가 활성 상태

### 기본 패키지와 보안 업데이트

먼저 SSH 서버와 방화벽, 인증서, 자동 보안 업데이트 도구를 확인·설치한다.

```bash
sudo apt update
sudo apt install -y \
  openssh-server ufw ca-certificates sudo unattended-upgrades
sudo apt upgrade -y
```

업데이트 뒤 재부팅이 필요한지 확인한다.

```bash
if test -f /var/run/reboot-required; then
  cat /var/run/reboot-required
  echo '재부팅이 필요합니다. SSH 키 검증 전이므로 현재 접속 정보를 확인한 뒤 재부팅합니다.'
else
  echo '현재 재부팅 요구 없음'
fi
```

재부팅이 필요하고 현재 접속 경로를 다시 확인할 수 있을 때만 실행한다.

```bash
sudo reboot
```

재접속 뒤 SSH 서비스 상태를 확인한다.

```bash
sudo systemctl enable --now ssh
sudo systemctl status ssh --no-pager
```

예상 결과: `Active: active (running)`이 보인다.

### 자동 보안 업데이트

학습 VM에도 보안 업데이트 정책은 명확히 둔다. 다만 커널 업데이트 뒤 자동 재부팅 여부는 사용 환경에 따라 달라질 수 있으므로, 처음에는 설정 상태만 확인한다.

```bash
sudo dpkg-reconfigure -plow unattended-upgrades
sudo systemctl status apt-daily-upgrade.timer --no-pager
```

이 VM은 실습 전용이므로, 커널이 업데이트되면 다음 실습 전에 재부팅하고 7절의 eBPF 기능 확인을 다시 수행한다.

## 4. 첫 접속과 전용 계정 준비

### 실행 위치를 먼저 구분하기

이 절에는 두 컴퓨터가 나온다. 명령을 실행하기 전에 아래 표에서 **어느 터미널에 입력하는지** 먼저 확인한다.

| 표기 | 실제 위치 | 하는 일 |
| --- | --- | --- |
| **[개발 PC]** | 지금 사용하는 Windows, macOS 또는 Linux PC | SSH 키를 만들고, 서버로 SSH 접속하며, VS Code를 실행한다. private key는 여기만 보관한다. |
| **[서버]** | 새로 만든 Ubuntu VM에 초기 계정으로 SSH 접속한 터미널 | ebpf-lab 계정을 만들고 public key를 등록한다. |

실제 진행 순서는 아래 세 단계다.

```text
1. [개발 PC] ssh-keygen으로 키 쌍 생성
2. [서버] ebpf-lab 계정 생성 후 public key를 authorized_keys에 등록
3. [개발 PC] 새 키로 ebpf-lab@서버에 로그인 성공 확인
```

아직 서버에 처음 로그인할 방법이 없다면, 클라우드 콘솔의 웹 터미널·초기 password·클라우드가 내려준 PEM private key 중 **제공자가 안내한 초기 접속 방법**으로 먼저 [서버]에 들어간다. 그 초기 연결은 공개키 등록이 끝날 때까지 유지한다.

### 4.1 먼저 이해하기: SSH 키와 PEM 파일의 역할

SSH 키 인증은 비밀번호 대신 **서로 짝인 두 파일**로 로그인하는 방식이다.

~~~text
개발 PC                                      서버의 ebpf-lab 계정
────────                                      ───────────────────
private key                                  authorized_keys
(절대 밖으로 보내지 않음)    ← 짝 →          public key 한 줄

로그인 시 private key로 증명                  public key로 증명을 검증
~~~

- **private key(개인키)**: 로그인하는 개발 PC에만 둔다. 비밀번호와 같은 비밀값이다.
- **public key(공개키)**: 서버의 `~/.ssh/authorized_keys`에 넣는다. 노출되어도 로그인에 직접 사용할 수 없는 검증용 값이다.
- **authorized_keys**: “이 계정에 어떤 공개키의 소유자가 로그인해도 되는가”를 적어 둔 서버 측 허용 목록이다. 공개키 한 줄이 한 개의 로그인 권한이다.

PEM은 키를 저장하는 **파일 형식 또는 확장자**다. 클라우드에서 내려받는 `something.pem`은 대부분 private key다. 반면 `ssh-keygen`이 만드는 확장자 없는 `id_ed25519`나 `id_rsa`도 private key다. 이름과 확장자는 달라도 역할은 같다.

> private key를 서버의 `authorized_keys`에 넣거나 SCP로 서버에 복사하면 안 된다. 서버에는 항상 public key만 넣는다.

### 4.2 [Windows 개발 PC] Ed25519 SSH 키 생성

Windows PowerShell에서 실행한다. 이 명령은 Windows 개발 PC에서만 실행하며, Ubuntu 서버에서 실행하지 않는다.

~~~powershell
ssh-keygen -t ed25519 -a 100 -f "$HOME\.ssh\ebpf-lab_ed25519" -C "ebpf-lab"
~~~

키 생성 중 `Enter passphrase`가 나오면 private key를 보호할 별도 암호를 설정하는 편을 권장한다. Ubuntu 계정 비밀번호와는 별개다.

생성 파일을 확인하고 public key 한 줄 전체를 복사한다.

~~~powershell
Get-ChildItem "$HOME\.ssh"
Get-Content "$HOME\.ssh\ebpf-lab_ed25519.pub"
~~~

생성 파일:

- `$HOME\.ssh\ebpf-lab_ed25519`: private key. 서버·채팅·Git에 올리지 않는다.
- `$HOME\.ssh\ebpf-lab_ed25519.pub`: public key. 다음 단계에서 서버에 등록한다.

### 4.3 [Ubuntu 서버] ebpf-lab 계정 생성

클라우드가 제공한 초기 sudo 계정으로 SSH 접속했거나, VM 콘솔에 접속한 상태에서 실행한다.

~~~bash
sudo adduser ebpf-lab
sudo usermod -aG sudo ebpf-lab
~~~

두 번째 명령은 eBPF 프로그램을 load·attach할 때 필요한 sudo를 허용한다. SSH 키 인증 자체에는 sudo 그룹이 필요하지 않지만, 이 실습 프로젝트에서는 필요하다.

### 4.4 [Ubuntu 서버] public key 등록 경로 준비

Ubuntu 서버에서 실행한다. 기존 `authorized_keys` 파일이 있어도 내용을 지우지 않는 `mkdir`, `touch`, `chown`, `chmod` 방식을 사용한다.

~~~bash
sudo mkdir -p /home/ebpf-lab/.ssh
sudo chown ebpf-lab:ebpf-lab /home/ebpf-lab/.ssh
sudo chmod 700 /home/ebpf-lab/.ssh

sudo touch /home/ebpf-lab/.ssh/authorized_keys
sudo chown ebpf-lab:ebpf-lab /home/ebpf-lab/.ssh/authorized_keys
sudo chmod 600 /home/ebpf-lab/.ssh/authorized_keys
sudo nano /home/ebpf-lab/.ssh/authorized_keys
~~~

`nano` 편집기가 열리면 Windows PowerShell에서 복사한 `ssh-ed25519 AAAA... ebpf-lab` 한 줄 전체를 붙여 넣는다. 기존 공개키가 있으면 지우지 말고 새 줄에 추가한다.

#### nano 최소 사용법

| 목적 | 키 입력 | 화면에서 할 일 |
| --- | --- | --- |
| public key 붙여 넣기 | 터미널의 붙여 넣기 단축키 | `ssh-ed25519 AAAA... ebpf-lab` 한 줄 전체를 넣는다. |
| 저장 | `Ctrl+O` | 아래쪽에 파일명이 보이면 변경하지 말고 `Enter`를 누른다. |
| 종료 | `Ctrl+X` | nano를 닫고 쉘 프롬프트로 돌아간다. |
| 저장하지 않고 종료 | `Ctrl+X` → `N` | 잘못 붙여 넣었을 때만 사용한다. |

저장했다면 아래 명령으로 서버에서 공개키가 한 줄 들어갔는지 확인한다. 출력값은 public key이므로 확인할 수 있지만, private key 내용은 절대 출력하거나 복사하지 않는다.

~~~bash
sudo cat /home/ebpf-lab/.ssh/authorized_keys
~~~

> `.pub`가 없는 private key 파일이나 `.pem` private key 파일의 내용을 붙여 넣으면 안 된다.

### 4.5 [Windows 개발 PC] SSH 접속 테스트

Windows PowerShell의 새 창에서 실행한다. 이 명령은 Ubuntu 서버에서 실행하지 않는다.

~~~powershell
ssh -i "$HOME\.ssh\ebpf-lab_ed25519" ebpf-lab@<SERVER_IP>
~~~

- `<SERVER_IP>`는 실제 Ubuntu 서버 IP 주소로 바꾼다.
- `-i` 뒤 경로는 Windows 개발 PC에 있는 private key 경로다.

접속에 성공한 뒤, 방금 열린 Ubuntu 서버 터미널에서 실행한다.

~~~bash
whoami
sudo -v
~~~

예상 결과: `whoami`는 `ebpf-lab`을 출력한다. 기존 초기 계정의 SSH 세션은 이 검증이 끝날 때까지 닫지 않는다.

접속에 실패하면 Windows PowerShell에서 아래 명령으로 상세 로그를 확인한다.

~~~powershell
ssh -v -i "$HOME\.ssh\ebpf-lab_ed25519" ebpf-lab@<SERVER_IP>
~~~

`-v`, `-vv`, `-vvv` 순으로 로그 상세도가 높아진다. 오류 원문은 `error/` 기록에 남긴다.

### 4.6 [Windows 개발 PC] SSH config와 VS Code Remote-SSH

Windows의 `$HOME\.ssh\config` 파일을 만들거나 수정한다.

~~~sshconfig
Host ebpf-lab
    HostName <SERVER_IP_OR_DOMAIN>
    User ebpf-lab
    Port 22
    IdentityFile ~/.ssh/ebpf-lab_ed25519
    IdentitiesOnly yes
~~~

| 설정 | 의미 |
| --- | --- |
| `Host` | Windows PC에서 사용할 접속 별칭 |
| `HostName` | 실제 서버 IP 또는 도메인 |
| `User` | Ubuntu 로그인 계정 |
| `Port` | SSH 포트. 기본값은 22 |
| `IdentityFile` | Windows PC의 private key 경로 |
| `IdentitiesOnly yes` | 설정한 private key만 사용하도록 제한 |

Windows PowerShell에서 별칭 접속을 확인한다.

~~~powershell
ssh ebpf-lab
~~~

VS Code에서는 Remote-SSH 확장을 설치한 뒤 `Remote-SSH: Connect to Host...`에서 `ebpf-lab`을 선택한다. 같은 SSH config를 읽으므로 별도 private key 업로드는 필요 없다.

### 4.7 [Ubuntu 서버] SSH 하드닝 — 키 접속 성공 뒤에만

Windows에서 `ssh ebpf-lab` 접속이 성공한 뒤에만 Ubuntu 서버에서 실행한다. 기존 초기 SSH 세션은 새 접속을 확인할 때까지 유지한다.

~~~bash
sudo nano /etc/ssh/sshd_config.d/99-ebpf-lab.conf
~~~

파일 내용:

~~~text
PermitRootLogin no
PasswordAuthentication no
KbdInteractiveAuthentication no
X11Forwarding no
AllowTcpForwarding no
AllowUsers ebpf-lab
~~~

문법 검사와 반영:

~~~bash
sudo sshd -t
sudo systemctl reload ssh
sudo sshd -T | grep -E 'permitrootlogin|passwordauthentication|kbdinteractiveauthentication|x11forwarding|allowtcpforwarding|allowusers'
~~~

예상 결과에는 `permitrootlogin no`, `passwordauthentication no`, `allowusers ebpf-lab`가 포함된다. 마지막으로 Windows 개발 PC에서 `ssh ebpf-lab`을 한 번 더 성공시킨 뒤에 기존 초기 세션을 닫는다.

## 5. 방화벽

클라우드 보안 그룹과 서버 방화벽을 모두 제한할 수 있으면 둘 다 사용한다. 아래 <MY_PUBLIC_IP>는 본인 IP 또는 CIDR로 교체한다.

~~~bash
sudo ufw allow from <MY_PUBLIC_IP> to any port 22 proto tcp
sudo ufw enable
sudo ufw status numbered
~~~

예상 결과: TCP 22가 지정 IP에서만 허용된다.

- VPN을 쓴다면 VPN egress IP를 먼저 확인한다.
- 현재 SSH 연결을 유지한 상태에서만 ufw enable을 실행한다.
- 웹 UI, DB, 원격 로그 포트는 이 학습 단계에서 열지 않는다.

## 6. eBPF 개발 도구 설치

전용 계정으로 로그인한 뒤 실행한다.

~~~bash
sudo apt update
sudo apt install -y \
  bpftrace bpftool \
  clang llvm libbpf-dev libelf-dev zlib1g-dev \
  linux-tools-common linux-tools-$(uname -r) linux-headers-$(uname -r) \
  build-essential pkg-config git
~~~

bpftool 패키지를 찾지 못하면 다음을 먼저 확인한다.

~~~bash
apt-cache policy bpftool linux-tools-common linux-tools-$(uname -r)
dpkg -L linux-tools-common | grep -E '/bpftool$' || true
command -v bpftool || command -v /usr/sbin/bpftool
~~~

Ubuntu 20.04 계열처럼 독립 bpftool 패키지가 없는 환경은 linux-tools-common이 제공하는 /usr/sbin/bpftool을 사용한다. 이 실습 서버의 기준은 Ubuntu 24.04 LTS다.

## 7. 설치 후 필수 기능 확인

이 결과가 날짜별 테스트 시나리오의 첫 단계 입력값이다.

~~~bash
uname -r
uname -m
test -r /sys/kernel/btf/vmlinux && echo 'BTF: available' || echo 'BTF: unavailable'

if sudo bpftool feature probe kernel | grep -Eqi 'map_type.*ringbuf|ringbuf'; then
  echo 'ring buffer: available'
else
  echo 'ring buffer: unavailable'
fi

if sudo bpftool feature probe kernel | grep -q 'bpf_get_current_task_btf'; then
  echo 'current task BTF helper: available'
else
  echo 'current task BTF helper: unavailable'
fi

test -r /sys/kernel/tracing/events/syscalls/sys_enter_execve/format && echo 'tracepoint: available' || echo 'tracepoint: unavailable'

bpftrace --version
bpftool version
clang --version
df -h /
free -h
~~~

### 성공 기준

- BTF: available
- ring buffer: available
- current task BTF helper: available
- tracepoint: available
- 패키지 설치 뒤 루트 파일시스템에 최소 10 GiB 여유 공간
- 메모리와 swap 상태를 기록할 수 있음

bpf_get_current_task_btf()는 Linux 5.11부터 제공된다. [helper 문서](https://docs.ebpf.io/linux/helper-function/bpf_get_current_task_btf/)

## 8. 프로젝트 배치

소스는 전용 계정의 홈 아래에 둔다. /usr/local/src나 root 홈은 이 학습 단계에서 필요하지 않다.

~~~bash
mkdir -p ~/workspace
cd ~/workspace

# Git 원격 저장소가 준비된 경우
# git clone <YOUR_REPOSITORY_URL> execmon

# 또는 개발 PC에서 공개키 인증을 이용해 프로젝트를 전송한 경우
# scp -i ~/.ssh/ebpf-lab_ed25519 -r ./ebpfProject ebpf-lab@<SERVER_IP>:~/workspace/execmon

cd ~/workspace/execmon
find . -maxdepth 2 -type f | sort
~~~

src/execmon.h, src/execmon.bpf.c, src/execmon.c가 보여야 한다. src/vmlinux.h와 build/는 서버에서 생성하므로 Git에 넣지 않는다.

## 9. 권한 원칙

- 컴파일, 소스 편집, 로그 읽기는 ebpf-lab 계정으로 한다.
- BPF object load와 attach가 필요한 실행만 sudo ./build/execmon으로 수행한다.
- 처음부터 비밀번호 없는 sudo를 부여하지 않는다.
- CAP_BPF, CAP_PERFMON 같은 capability 세분화는 이후 단계에서 별도로 검토한다.
- 자동 차단, 프로세스 종료, 네트워크 차단은 구현하지 않는다.

## 10. MCP 연결을 나중에 고려할 때

이번 서버 생성 단계에서 MCP 연결은 필수가 아니다. 나중에 연결한다면 SSH private key를 채팅에 전달하지 않는다.

~~~text
Codex → 제한된 MCP 도구 → MCP 내부의 전용 SSH 키 → ebpf-lab VM
~~~

허용 도구는 환경 조회, 프로젝트 내부 빌드, execmon 시작·중지, 로그 조회, BPF 상태 조회처럼 좁게 만든다. 임의 문자열을 받는 execute_shell(command) 하나로 모든 명령을 실행하게 만들지 않는다.

## 11. 서버 생성 완료 체크리스트

- [ ] Ubuntu Server 24.04 LTS x86_64 VM을 만들었다.
- [ ] 2 vCPU / 4 GiB RAM / 30 GiB SSD 이상으로 만들었다.
- [ ] UEFI 환경이면 EFI 512 MiB, `/boot` 1 GiB, LVM root 18 GiB, home 7 GiB, swap 2 GiB로 나누거나 동등한 LVM 구성을 확인했다.
- [ ] `/home`에서 학습 바이너리를 실행해야 하므로 `noexec` 옵션을 사용하지 않았다.
- [ ] 호스트 이름, Asia/Seoul 시간대, NTP 상태를 확인했다.
- [ ] 기본 보안 업데이트와 SSH 서비스 상태를 확인했다.
- [ ] SSH 22 포트를 본인 IP 또는 bastion으로만 제한했다.
- [ ] ebpf-lab 전용 계정과 SSH 공개키 로그인을 만들었다.
- [ ] root·비밀번호 SSH 로그인을 비활성화하고, 새 세션에서 키 로그인을 재확인했다.
- [ ] BTF, ring buffer, bpf_get_current_task_btf, tracepoint를 확인했다.
- [ ] bpftool, bpftrace, clang, libbpf 개발 의존성을 설치했다.
- [ ] 소스를 ~/workspace/execmon에 배치했다.
- [ ] 패키지 설치 전 또는 후 스냅샷을 만들었다.
- [ ] 서버 비밀값을 채팅·문서·Git에 기록하지 않았다.

## 12. 다음 단계

이 체크리스트가 끝나면 [날짜별 테스트 시나리오](scenario/2026-10-25_execmon_테스트_시나리오.md)의 환경 점검 단계부터 진행한다.

관련 문서:

- [execmon 테스트 시나리오](scenario/2026-10-25_execmon_테스트_시나리오.md)
- [환경 적합성 흐름도](scenario/flows/2026-10-25_환경_적합성_흐름.md)
- [도구 명령어 레퍼런스](tool_commands.md)

