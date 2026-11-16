// SPDX-License-Identifier: BSD-2-Clause
/*
 * 이 파일은 일반 Linux 사용자 공간에서 실행되는 C 프로그램이다.
 * Java로 비유하면 main()을 가진 콘솔 애플리케이션이며, BPF 코드를 컴파일·검증하여
 * 커널에 올리고 ring buffer에서 이벤트를 꺼내 출력하는 역할을 한다.
 */
#include <errno.h>
#include <signal.h>
#include <stdio.h>

/* libbpf: BPF object, map, ring buffer를 C에서 다루는 라이브러리 */
#include <bpf/libbpf.h>

/* BPF 프로그램과 정확히 같은 이벤트 바이트 구조를 해석하기 위한 헤더 */
#include "execmon.h"

/*
 * bpftool gen skeleton이 execmon.bpf.o로부터 생성하는 헤더다.
 * Java의 생성된 client 코드처럼, object/map/program을 타입 안전한 함수와 구조체로
 * 노출한다. 다음 단계에서 build/execmon.skel.h를 생성해 include 경로에 넣는다.
 */
#include "execmon.skel.h"

/*
 * signal handler와 main이 함께 보는 종료 플래그다. sig_atomic_t는 signal handler에서
 * 안전하게 읽고 쓸 수 있는 최소 정수 타입이다. volatile은 컴파일러가 while 조건을
 * 레지스터 값으로만 고정하지 않게 한다.
 */
static volatile sig_atomic_t exiting;

/* Ctrl-C(SIGINT) 또는 systemd 등의 종료 요청(SIGTERM)을 받으면 호출된다. */
static void handle_signal(int signal_number)
{
	(void)signal_number; /* Java의 사용하지 않는 매개변수와 같은 경고를 피한다. */
	exiting = 1;         /* handler 안에서는 출력·메모리 해제 대신 플래그만 바꾼다. */
}

/*
 * ring buffer에 이벤트가 도착할 때 libbpf가 호출하는 callback이다.
 * Java라면 Consumer<ExecmonEvent>의 accept()에 해당한다.
 *
 * data는 커널에서 보낸 바이트의 시작 주소다. 이 코드의 event 포인터는 새 객체를
 * 생성한 것이 아니라 해당 바이트를 struct execmon_event 형태로 해석하는 것이다.
 */
static int handle_event(void *context, void *data, size_t data_size)
{
	const struct execmon_event *event = data;

	(void)context; /* 지금은 callback에 별도 상태를 넘기지 않는다. */

	/* 예상보다 짧은 데이터면 필드를 읽지 않아 잘못된 메모리 접근을 막는다. */
	if (data_size < sizeof(*event)) {
		fprintf(stderr, "received truncated event: %zu bytes\n", data_size);
		return 0;
	}

	/*
	 * %-8u 같은 printf 형식:
	 *   %u  : unsigned int를 10진수로 출력
	 *   -8  : 최소 8칸을 쓰고 왼쪽 정렬(표 형태 가독성)
	 *   %s  : NUL로 끝나는 C 문자열 출력
	 *
	 * Java의 System.out.printf(...)와 같은 목적이다. 첫 버전은 사람이 보기 쉬운
	 * 텍스트를 우선한다. JSON은 escaping과 길이 정책을 설계한 뒤 추가한다.
	 */
	printf("pid=%-8u ppid=%-8u uid=%-6u comm=%-15s file=%s\n",
	       event->pid,
	       event->ppid,
	       event->uid,
	       event->comm,
	       event->filename);
	fflush(stdout); /* 버퍼가 찰 때까지 기다리지 않고 한 이벤트씩 화면/파이프로 보낸다. */
	return 0;       /* 0은 정상 처리. 음수 반환은 poll 쪽에 오류로 전달될 수 있다. */
}

int main(void)
{
	/*
	 * skeleton은 BPF object, programs, maps, links를 모아 둔 관리 객체다.
	 * ring_buffer는 events map을 읽는 사용자 공간 소비자 객체다.
	 */
	struct execmon_bpf *skeleton;
	struct ring_buffer *ring_buffer;
	int error;

	/* Java shutdown hook과 비슷한 목적: Ctrl-C 뒤에도 아래 cleanup을 실행한다. */
	signal(SIGINT, handle_signal);
	signal(SIGTERM, handle_signal);

	/*
	 * open_and_load는 세 일을 차례로 수행한다.
	 * 1) BPF ELF object를 연다.
	 * 2) map을 만들고 CO-RE relocation을 적용한다.
	 * 3) verifier에게 프로그램을 검사받아 커널에 로드한다.
	 * 실패 시 NULL이며, verifier 로그가 표준 오류에 추가로 나올 수 있다.
	 */
	skeleton = execmon_bpf__open_and_load();
	if (!skeleton) {
		fprintf(stderr, "failed to open and load execmon BPF object\n");
		return 1;
	}

	/* attach는 로드된 handle_execve 프로그램을 실제 tracepoint에 연결한다. */
	error = execmon_bpf__attach(skeleton);
	if (error) {
		fprintf(stderr, "failed to attach execmon BPF program: %d\n", error);
		goto cleanup_skeleton;
	}

	/*
	 * events map의 file descriptor를 libbpf에 넘겨 ring buffer 소비자를 만든다.
	 * handle_event가 이벤트마다 호출되며, NULL은 callback context가 없다는 뜻이다.
	 */
	ring_buffer = ring_buffer__new(bpf_map__fd(skeleton->maps.events),
	                               handle_event, NULL, NULL);
	if (!ring_buffer) {
		error = -errno; /* C API는 흔히 errno에 실제 오류 번호를 남긴다. */
		fprintf(stderr, "failed to create ring buffer: %d\n", error);
		goto cleanup_skeleton;
	}

	fprintf(stderr, "execmon started; press Ctrl-C to stop\n");
	while (!exiting) {
		/* 최대 250 ms 기다리며 도착한 이벤트를 callback으로 전달한다. */
		error = ring_buffer__poll(ring_buffer, 250);
		if (error == -EINTR) {
			/* signal로 poll이 깨어난 경우는 오류가 아니라 정상 종료 흐름이다. */
			error = 0;
			break;
		}
		if (error < 0) {
			fprintf(stderr, "ring buffer poll failed: %d\n", error);
			break;
		}
	}

	/* Java의 try-with-resources/finally처럼, 만든 사용자 공간 객체를 먼저 해제한다. */
	ring_buffer__free(ring_buffer);

cleanup_skeleton:
	/* BPF link를 detach하고 map/object 관련 리소스를 정리한다. */
	execmon_bpf__destroy(skeleton);

	/* main의 0은 성공, 양수는 쉘에 전달할 종료 코드다. */
	return error < 0 ? -error : error;
}
