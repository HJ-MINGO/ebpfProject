# Windows 개발 PC ↔ Ubuntu 서버 SSH 공개키·개인키 인증 설정 가이드

## 1. 동작 원리

- **Windows 개발 PC(SSH 클라이언트)**: 개인키와 공개키를 생성하고 개인키를 보관합니다.
- **Ubuntu 서버(SSH 서버)**: 로그인 대상 계정의 `~/.ssh/authorized_keys`에 공개키를 등록합니다.
- **로그인**: 클라이언트는 개인키로 인증 데이터에 서명하고, 서버는 등록된 공개키로 이를 검증합니다. 개인키 자체는 서버로 전송되지 않습니다.

> 이 문서는 Windows PowerShell과 Ubuntu 서버에서 `ebpf-lab` 계정으로 접속하는 예시입니다. IP `192.168.56.101`은 실제 서버 주소로 변경하세요.

## 2. Windows PC에서 SSH 키 생성

PowerShell에서 실행:

```powershell
ssh-keygen -t ed25519 -a 100 -f "$HOME\.ssh\ebpf-lab_ed25519" -C "ebpf-lab"
```

| 항목 | 설명 |
|---|---|
| `ssh-keygen` | SSH 키 생성 및 관리 |
| `-t ed25519` | Ed25519 키 알고리즘 사용 |
| `-a 100` | 개인키 passphrase 보호에 쓰이는 KDF 반복 횟수 |
| `-f` | 개인키를 저장할 파일 경로(공개키는 `.pub` 확장자 추가) |
| `-C "ebpf-lab"` | 공개키 끝에 표시할 식별용 주석 |

생성 중 `Enter passphrase` 메시지가 나오면 개인키 보호용 암호를 지정하는 것이 좋습니다. 이는 Ubuntu 계정 비밀번호와 별개입니다.
(실무에서는 passphrase를 설정하는 편을 권장합니다.)

생성 파일:

- `$HOME\.ssh\ebpf-lab_ed25519`: **개인키. 공유하거나 서버에 업로드하지 않습니다.**
- `$HOME\.ssh\ebpf-lab_ed25519.pub`: **공개키. 서버에 등록할 파일입니다.**

확인:

```powershell
Get-ChildItem "$HOME\.ssh"
Get-Content "$HOME\.ssh\ebpf-lab_ed25519.pub"
```

출력되는 `ssh-ed25519 AAAA... ebpf-lab` 형태의 한 줄 전체를 복사합니다.

## 3. Ubuntu 서버에서 계정 생성

기존 sudo 사용 가능 계정으로 로그인하거나 VM 콘솔에서 실행:

```bash
sudo adduser ebpf-lab
```

관리자 권한이 실제로 필요하다면 다음을 추가합니다(SSH 키 인증 자체에는 불필요).

```bash
sudo usermod -aG sudo ebpf-lab
```

- `usermod`: 기존 사용자 속성 변경
- `-aG sudo`: 기존 보조 그룹을 유지하면서 `sudo` 그룹 추가

## 4. Ubuntu 서버의 공개키 등록 경로 준비

```bash
sudo mkdir -p /home/ebpf-lab/.ssh
sudo chown ebpf-lab:ebpf-lab /home/ebpf-lab/.ssh
sudo chmod 700 /home/ebpf-lab/.ssh

sudo touch /home/ebpf-lab/.ssh/authorized_keys
sudo chown ebpf-lab:ebpf-lab /home/ebpf-lab/.ssh/authorized_keys
sudo chmod 600 /home/ebpf-lab/.ssh/authorized_keys
```

- `mkdir -p`: 부모 경로를 포함한 디렉터리 생성
- `chown 사용자:그룹`: 소유자와 소유 그룹 설정
- `chmod 700`: 디렉터리 소유자에게만 접근 권한
- `chmod 600`: 파일 소유자만 읽기/쓰기 권한
- `touch`: 파일이 없다면 생성하고, 기존 파일은 내용 유지

파일 편집:

```bash
sudo nano /home/ebpf-lab/.ssh/authorized_keys
```

Windows에서 확인한 `.pub` **공개키 한 줄 전체**를 붙여 넣고 저장합니다. 기존 키가 있다면 지우지 말고 새 줄에 추가합니다.

> `authorized_keys`에 개인키 또는 클라우드에서 내려받은 비밀 `.pem` 내용을 붙여 넣으면 안 됩니다.

## 5. Windows에서 SSH 접속 테스트

```powershell
ssh -i "$HOME\.ssh\ebpf-lab_ed25519" ebpf-lab@192.168.56.101
```

- `-i`: 로그인 인증에 사용할 **개인키** 경로
- `ebpf-lab`: Ubuntu 서버 계정명
- `192.168.56.101`: Ubuntu 서버 IP 주소(예시)

실패 시 디버그 로그:

```powershell
ssh -v -i "$HOME\.ssh\ebpf-lab_ed25519" ebpf-lab@192.168.56.101
```

`-v`, `-vv`, `-vvv` 순으로 로그 상세도가 높아집니다.

## 6. SSH config로 간단하게 접속

Windows에서 `$HOME\.ssh\config` 파일을 생성하거나 수정:

```sshconfig
Host ebpf-lab
    HostName 192.168.56.101
    User ebpf-lab
    Port 22
    IdentityFile ~/.ssh/ebpf-lab_ed25519
    IdentitiesOnly yes
```

| 설정 | 설명 |
|---|---|
| `Host` | 로컬에서 사용할 접속 별칭 |
| `HostName` | 실제 서버 IP 또는 도메인 |
| `User` | SSH 로그인 사용자 |
| `Port` | SSH 접속 포트(기본 22) |
| `IdentityFile` | 사용할 로컬 개인키 |
| `IdentitiesOnly yes` | 명시적으로 구성한 인증 ID만 사용하도록 제한 |

이후 PowerShell 또는 VS Code Remote-SSH에서 `ebpf-lab` 별칭 사용:

```powershell
ssh ebpf-lab
```

## 7. 가이드에 등장했던 `install` 명령 이해

```bash
sudo install -d -m 700 -o ebpf-lab -g ebpf-lab /home/ebpf-lab/.ssh
```

`install`은 여기서 프로그램 패키지를 설치하는 명령이 아니라 파일/디렉터리를 만들면서 소유자와 권한을 함께 지정하는 명령입니다.

- `-d`: 디렉터리 생성
- `-m 700`: 접근 권한
- `-o ebpf-lab`: 소유자
- `-g ebpf-lab`: 그룹

다음 명령은 **기존 파일 내용을 덮어쓸 수 있으니**, 기존 `authorized_keys`가 있는 운영 서버에서는 피합니다.

```bash
sudo install -m 600 -o ebpf-lab -g ebpf-lab /dev/null /home/ebpf-lab/.ssh/authorized_keys
```

기존 키를 유지하려면 앞서 설명한 `touch`, `chown`, `chmod`와 편집 방식을 사용하세요.

## 8. 실무 점검 사항

1. 개인키는 클라이언트에만 보관하고, 서버에는 공개키만 등록합니다.
2. 하나의 `authorized_keys`에 여러 공개키를 한 줄씩 등록할 수 있습니다.
3. 키 인증이 정상 동작하기 전에는 서버의 비밀번호 인증을 끄지 않습니다.
4. VM 네트워크에 따라 호스트에서 게스트 IP에 직접 접속할 수 없을 수 있습니다. VirtualBox NAT만 사용한다면 포트 포워딩 또는 호스트 전용 네트워크를 고려하세요.
5. 새 환경에는 일반적으로 Ed25519를 권장하며, 오래된 장비 호환성 등이 필요할 때 RSA를 선택할 수 있습니다.

## 9. 전체 명령 흐름(요약)

**Windows PowerShell:**

```powershell
ssh-keygen -t ed25519 -a 100 -f "$HOME\.ssh\ebpf-lab_ed25519" -C "ebpf-lab"
Get-Content "$HOME\.ssh\ebpf-lab_ed25519.pub"
```

**Ubuntu 서버:**

```bash
sudo adduser ebpf-lab
sudo mkdir -p /home/ebpf-lab/.ssh
sudo chown ebpf-lab:ebpf-lab /home/ebpf-lab/.ssh
sudo chmod 700 /home/ebpf-lab/.ssh
sudo touch /home/ebpf-lab/.ssh/authorized_keys
sudo chown ebpf-lab:ebpf-lab /home/ebpf-lab/.ssh/authorized_keys
sudo chmod 600 /home/ebpf-lab/.ssh/authorized_keys
sudo nano /home/ebpf-lab/.ssh/authorized_keys
# Windows에서 복사한 공개키 한 줄을 추가 후 저장
```

**Windows PowerShell 접속:**

```powershell
ssh -i "$HOME\.ssh\ebpf-lab_ed25519" ebpf-lab@192.168.56.101
```
