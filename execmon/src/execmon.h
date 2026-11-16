/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef __EXECMON_H
#define __EXECMON_H

/*
 * 이 헤더는 Java로 비유하면 BPF 쪽과 loader 쪽이 함께 사용하는 DTO(Data
 * Transfer Object) 정의다. 한쪽에서만 필드를 바꾸면, 다른 쪽은 잘못된
 * 바이트 위치를 읽으므로 반드시 두 프로그램이 같은 파일을 include한다.
 */
#include <linux/types.h>

/*
 * Linux의 comm은 프로세스 이름을 위한 고정 길이 배열이다.
 * 끝의 NUL 문자까지 포함해 최대 16바이트이므로, 보이는 이름은 15글자에서
 * 잘릴 수 있다. Java String처럼 길이가 가변인 객체가 아니다.
 */
#define EXECMON_COMM_LEN 16

/* 파일 경로는 첫 학습 버전에서 최대 255글자 + NUL 문자까지만 저장한다. */
#define EXECMON_FILENAME_LEN 256

/*
 * ring buffer로 한 번에 넘길 이벤트의 메모리 배치다.
 *
 * Java의 객체와 달리 C 구조체에는 객체 헤더나 String 참조가 들어가지 않는다.
 * 각 필드는 이 선언 순서와 크기 그대로 연속된 바이트가 된다. 따라서 comm과
 * filename도 char 포인터가 아니라 실제 문자 배열로 선언한다.
 *
 * argv(명령행 인자), envp(환경 변수)는 비밀번호·토큰을 포함할 수 있어
 * 의도적으로 수집하지 않는다.
 */
struct execmon_event {
	__u32 pid;                           /* execve를 호출한 프로세스 ID */
	__u32 ppid;                          /* 해당 프로세스의 부모 프로세스 ID */
	__u32 uid;                           /* 실행 사용자의 user ID */
	char comm[EXECMON_COMM_LEN];         /* 호출 당시 프로세스 이름 */
	char filename[EXECMON_FILENAME_LEN]; /* execve의 대상 파일 경로 */
};

#endif /* __EXECMON_H */
