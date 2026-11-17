// SPDX-License-Identifier: Dual BSD/GPL
/*
 * 이 파일은 일반 C 프로그램이 아니라 커널 안에서 실행될 작은 eBPF 프로그램이다.
 * Java의 JVM처럼 eBPF verifier가 로드 전에 모든 경로를 검사한다. 따라서 임의의
 * 포인터 역참조, 무한 반복, malloc, printf 같은 일반 사용자 공간 기능은 쓸 수 없다.
 *
 * 실행 위치:
 *   execve 시스템 콜 진입 → sys_enter_execve tracepoint → handle_execve()
 */

/*
 * vmlinux.h는 대상 Linux 커널의 BTF(Binary Type Format)에서 생성한 C 타입 목록이다.
 * task_struct 같은 커널 구조체를 CO-RE 방식으로 읽기 위해 필요하다.
 * 아직 파일을 만들지 않았으므로 이 소스는 다음 단계 전까지 컴파일되지 않는 것이 정상이다.
 */
#include "vmlinux.h"

/* BPF 프로그램과 Java의 DTO 역할을 하는 공용 이벤트 구조체 */
#define EXECMON_BPF 1
#include "execmon.h"

/* BPF_CORE_READ: 커널 버전별 구조체 필드 위치 차이를 로드 시 조정하는 도우미 */
#include <bpf/bpf_core_read.h>

/* SEC, bpf_ringbuf_reserve 등 eBPF helper/API 선언 */
#include <bpf/bpf_helpers.h>

/*
 * map은 eBPF 프로그램과 사용자 공간이 함께 접근하는 커널 안의 자료구조다.
 * 여기서는 ring buffer를 사용한다. Java의 BlockingQueue와 비슷하게 생각할 수
 * 있지만, 커널이 제공하는 고성능 이벤트 전달용 큐다.
 *
 * type: map 종류. BPF_MAP_TYPE_RINGBUF는 Linux 5.8 이상에서 지원된다.
 *       단, 이 프로그램은 아래 PPID 수집에 Linux 5.11부터의
 *       bpf_get_current_task_btf()도 사용하므로 실제 최소 기준은 Linux 5.11이다.
 * max_entries: ring buffer 전체 크기(256 KiB). ring buffer에서는 항목 수가 아니라
 *              바이트 단위 용량이다.
 * SEC(".maps"): 이 변수는 일반 전역 변수가 아니라 map 정의라는 ELF 섹션 표식이다.
 */
struct {
	__uint(type, BPF_MAP_TYPE_RINGBUF);
	__uint(max_entries, 256 * 1024);
} events SEC(".maps");

/*
 * SEC는 컴파일 결과의 ELF 섹션 이름을 지정한다. libbpf는 이 이름을 보고
 * "syscalls:sys_enter_execve tracepoint에 붙일 프로그램"이라고 판단한다.
 *
 * ctx는 tracepoint가 전달한 원시 이벤트 레코드다. 이 tracepoint에서 ctx->args[0]은
 * execve(const char *filename, ...)의 첫 번째 인자, 즉 파일명 포인터다.
 * sys_enter는 실행 '시도' 시점이므로 execve 실패도 관찰될 수 있다.
 */
SEC("tracepoint/syscalls/sys_enter_execve")
int handle_execve(struct trace_event_raw_sys_enter *ctx)
{
	/* Java의 지역 변수와 같지만, verifier가 초기화 여부와 안전한 사용을 검사한다. */
	struct execmon_event *event;
	struct task_struct *task;
	const char *filename;
	__u64 pid_tgid;

	/*
	 * ring buffer에 이벤트 한 건이 들어갈 공간을 먼저 예약한다.
	 * malloc와 비슷해 보이지만 예약할 대상은 사용자 힙이 아니라 events map이다.
	 * 공간이 없으면 NULL이다. 이 경우 절대 event에 접근하지 않고 이번 이벤트만
	 * 버린다. 향후에는 이 횟수를 별도 map으로 세어 드롭률을 측정한다.
	 */
	event = bpf_ringbuf_reserve(&events, sizeof(*event), 0);
	if (!event)
		return 0;

	/*
	 * helper는 verifier가 허용한 커널 제공 함수다.
	 * bpf_get_current_pid_tgid()의 상위 32비트는 TGID(일반적으로 ps에서 보는 PID),
	 * 하위 32비트는 현재 thread ID다. 여기서는 상위 값을 PID로 기록한다.
	 */
	pid_tgid = bpf_get_current_pid_tgid();
	event->pid = pid_tgid >> 32;

	/* 반환값의 하위 32비트가 UID, 상위 32비트가 GID다. 여기서는 UID만 쓴다. */
	event->uid = (__u32)bpf_get_current_uid_gid();

	/* 현재 task의 comm을 고정 길이 배열로 복사한다. */
	bpf_get_current_comm(event->comm, sizeof(event->comm));

	/*
	 * 현재 task에서 부모 task의 tgid를 읽어 PPID를 얻는다.
	 * BPF_CORE_READ는 단순 task->real_parent->tgid처럼 고정 오프셋을 믿지 않는다.
	 * 로드 시 BTF를 사용해 현재 커널에서 이 필드가 있는 실제 위치로 보정한다.
	 */
	task = (struct task_struct *)bpf_get_current_task_btf();
	event->ppid = BPF_CORE_READ(task, real_parent, tgid);

	/*
	 * ctx->args[0]은 사용자 공간 문자열을 가리킨다. 커널 eBPF 프로그램이
	 * filename[0]처럼 직접 읽으면 verifier가 거부할 수 있으므로,
	 * bpf_probe_read_user_str helper로 제한된 길이만 안전하게 복사한다.
	 */
	filename = (const char *)ctx->args[0];
	bpf_probe_read_user_str(event->filename, sizeof(event->filename), filename);

	/* 예약한 이벤트를 큐에 게시한다. 이후 loader의 callback이 이 바이트를 읽는다. */
	bpf_ringbuf_submit(event, 0);
	return 0; /* tracepoint 프로그램의 반환값은 사용자에게 직접 전달되지 않는다. */
}

/* eBPF 프로그램을 GPL 호환으로 표시한다. 일부 helper 사용에는 이 표기가 필요하다. */
char LICENSE[] SEC("license") = "Dual BSD/GPL";
