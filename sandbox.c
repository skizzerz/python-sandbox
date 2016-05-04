#include <Python.h>
#include <seccomp.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/resource.h>
#include <ucontext.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "sbcontext.h"
#include "sbdebug.h"

void report_bad_syscall(int signal, siginfo_t *info, void *context);

int main(int argc, char *argv[])
{
	int ret = -1;
	unsigned long mem_limit, cpu_limit;
	struct rlimit rl;
	wchar_t *program = NULL;
	size_t programlen = 0;
	FILE *mainpy = NULL;
#ifndef SB_NOSB
	int seccomp_action = SCMP_ACT_KILL;
	scmp_filter_ctx ctx = NULL;
#endif

	// verify we have all necessary args and that fds 3 and 4 have been opened for us
	if (argc < 4) {
		fprintf(stderr, "Usage: %s path_to_python memory_limit_bytes cpu_limit_secs\n", argv[0]);
		goto cleanup;
	}

	ret = fcntl(3, F_GETFD);
	if (ret < 0) {
		fprintf(stderr, "%s: Must be run as a child process with fds 3 and 4 opened.\n", argv[0]);
		goto cleanup;
	}
	
	ret = fcntl(4, F_GETFD);
	if (ret < 0) {
		fprintf(stderr, "%s: Must be run as a child process with fds 3 and 4 opened.\n", argv[0]);
		goto cleanup;
	}

	mem_limit = strtoul(argv[2], NULL, 10);
	cpu_limit = strtoul(argv[3], NULL, 10);

	if (mem_limit == 0)
		mem_limit = DEF_MEMORY;

	if (cpu_limit == 0)
		cpu_limit = DEF_CPU;

	// set resource limits, we limit our address space and cpu, and disable core dumps
	rl.rlim_cur = mem_limit;
	rl.rlim_max = mem_limit;
	ret = setrlimit(RLIMIT_AS, &rl);
	if (ret < 0)
		goto cleanup;

	rl.rlim_cur = 0;
	rl.rlim_max = 0;
	ret = setrlimit(RLIMIT_CORE, &rl);
	if (ret < 0)
		goto cleanup;

	rl.rlim_cur = cpu_limit;
	rl.rlim_max = cpu_limit;
	ret = setrlimit(RLIMIT_CPU, &rl);
	if (ret < 0)
		goto cleanup;

#ifndef SB_NOSB
#ifdef SB_DEBUG
	// set up our SIGSYS handler; any disallowed syscalls are trapped by this handler
	// and reported to the parent process to aid in debugging
	// if this ends up getting a SIGSYS that wasn't generated by seccomp, it exits the program
	struct sigaction sa;
	sigset_t mask;
	sigfillset(&mask); // block all other signals while ours is running
	sa.sa_sigaction = report_bad_syscall;
	sa.sa_mask = mask;
	sa.sa_flags = SA_SIGINFO;
	sigaction(SIGSYS, &sa, NULL);
	seccomp_action = SCMP_ACT_TRAP;
#endif

	// set up seccomp, we allow the following syscalls:
	// read() - fd 3 only
	// write() - fd 4 only
	// sigreturn(), rt_sigreturn() - needed for signal handlers
	// brk(), mmap(), mmap2(), mremap(), munmap() - needed for memory allocation (malloc/free)
	// exit(), exit_group() - so program can terminate
	ctx = seccomp_init(seccomp_action);

	if (ctx == NULL)
		goto cleanup;

	ret = seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(read), 1, SCMP_A0(SCMP_CMP_EQ, PIPEIN));
	if (ret < 0)
		goto cleanup;

	ret = seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(write), 1, SCMP_A0(SCMP_CMP_EQ, PIPEOUT));
	if (ret < 0)
		goto cleanup;

#ifdef SB_DEBUG
	// if debugging, allow sandbox to write to stdout and stderr
	ret = seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(write), 1, SCMP_A0(SCMP_CMP_EQ, 1));
	if (ret < 0)
		goto cleanup;

	ret = seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(write), 1, SCMP_A0(SCMP_CMP_EQ, 2));
	if (ret < 0)
		goto cleanup;	
#endif

	ret = seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(sigreturn), 0);
	if (ret < 0)
		goto cleanup;

	ret = seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(rt_sigreturn), 0);
	if (ret < 0)
		goto cleanup;

	ret = seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(exit_group), 0);
	if (ret < 0)
		goto cleanup;

	ret = seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(exit), 0);
	if (ret < 0)
		goto cleanup;

	ret = seccomp_load(ctx);
	if (ret < 0)
		goto cleanup;
#endif

	// initialize python interpreter -- this is initialized AFTER sandbox is set up
	// so that the python path can be faked (in essence, this allows for the parent proc
	// to implement a pseudo-chroot by specifying a virtual path to python).
	// This also ensures that anything done as part of the python initialization cannot
	// be used as a means to break out of the sandbox.
	programlen = mbstowcs(NULL, argv[1], 0) + 1;
	program = (wchar_t *)malloc(programlen * sizeof(wchar_t));
	if (mbstowcs(program, argv[1], programlen) == (size_t)-1) {
		ret = -1;
		fprintf(stderr, "%s: Cannot decode python path.\n", argv[0]);
		goto cleanup;
	}

	Py_SetProgramName(program);
	Py_Initialize();

	// init the python side of things by populating libraries, etc.
	// We expect that the parent proc provides a "main.py" file somewhere which contains
	// this initialization code, as well as the code to run whatever the user wanted.
	mainpy = fopen("main.py", "r");
	if (mainpy == NULL) {
		ret = errno;
		fprintf(stderr, "%s: Cannot open main.py.\n", argv[0]);
		goto cleanup;
	}

	// this closes mainpy after completion so we don't need to fclose it in cleanup
	ret = PyRun_SimpleFile(mainpy, "main.py");

cleanup:
	Py_Finalize();
	if (program != NULL)
		free(program);
#ifndef SB_NOSB
	seccomp_release(ctx);
#endif
	return -ret;
}

#ifdef SB_DEBUG
void report_bad_syscall(int signal, siginfo_t *siginfo, void *void_ctx)
{
	const char *name;
	int found = 0, first = 1, i, j;
	// SB_P*(ctx) can be used to get first 6 params passed to syscall (P1-P6)
	ucontext_t *ctx = (ucontext_t *)void_ctx;

	if (signal != SIGSYS || siginfo->si_code != SYS_SECCOMP) {
		// signal was not generated by seccomp, ignore it
		return;
	}

	if (siginfo->si_syscall >= nsyscalls) {
		// invalid or unknown syscall number
		fprintf(stderr, "Unknown syscall number %d\n", siginfo->si_syscall);
	}

	name = syscalls[siginfo->si_syscall];
	for (i = 0; arg_map[i].sys != NULL; ++i) {
		if (!strcmp(name, arg_map[i].sys)) {
			found = 1;
			break;
		}
	}

	if (found) {
		greg_t args[] = {SB_P1(ctx), SB_P2(ctx), SB_P3(ctx), SB_P4(ctx), SB_P5(ctx), SB_P6(ctx)};
		fprintf(stderr, "Bad syscall %s(", name);
		for (j = 0; j < arg_map[i].nargs; ++j) {
			if (first) {
				first = 0;
			} else {
				fputs(", ", stderr);
			}

			switch (arg_map[i].args[j]) {
			case 's':
				fprintf(stderr, "\"%s\"", (const char *)args[j]);
				break;
			case 'd':
				fprintf(stderr, "%lld", (long long int)args[j]);
				break;
			case 'x':
				fprintf(stderr, "0x%llx", (unsigned long long int)args[j]);
				break;
			case 'o':
				fprintf(stderr, "0%llo", (unsigned long long int)args[j]);
				break;
			case 'u':
				fprintf(stderr, "%llu", (unsigned long long int)args[j]);
				break;
			case 'f':
				fprintf(stderr, "%f", (double)args[j]);
				break;
			}
		}
		fputs(")\n", stderr);
	} else {
		fprintf(stderr, "Bad syscall %s(%lld, %lld, %lld, %lld, %lld, %lld)\n",
			name, SB_P1(ctx), SB_P2(ctx), SB_P3(ctx), SB_P4(ctx), SB_P5(ctx), SB_P6(ctx));
	}

	exit(-SIGSYS);
}
#endif
