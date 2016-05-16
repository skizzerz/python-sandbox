#include <Python.h>
#include <seccomp.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/mman.h>
#include <ucontext.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <execinfo.h>
#include <dlfcn.h>

#include "sbcontext.h"
#include "sblibc.h"

void sigsys_handler(int signal, siginfo_t *info, void *context);

int main(int argc, char *argv[])
{
	int ret = -1;
	unsigned long mem_limit, cpu_limit;
	struct rlimit rl;
	wchar_t *program = NULL;
	size_t programlen = 0;
	FILE *mainpy = NULL;
	scmp_filter_ctx ctx = NULL;

#ifdef SB_DEBUG
	// not used, it's here to force loading of the relevant .sos before the sandbox inits
	// so that we can safely print backtraces in the sigtrap handler
	void *bt_buffer[32];
	int bt_n = backtrace(bt_buffer, 32);
	char **bt_sym = backtrace_symbols(bt_buffer, bt_n);
	free(bt_sym);
#endif

	// verify we have all necessary args and that fds 3 and 4 have been opened for us
	if (argc < 4) {
		fprintf(stderr, "Usage: %s path_to_python memory_limit_bytes cpu_limit_secs\n", argv[0]);
		goto cleanup;
	}

	ret = fcntl(PIPEIN, F_GETFD);
	if (ret < 0) {
		fprintf(stderr, "%s: Must be run as a child process with fd 3 opened.\n", argv[0]);
		goto cleanup;
	}
	
	ret = fcntl(PIPEOUT, F_GETFD);
	if (ret < 0) {
		fprintf(stderr, "%s: Must be run as a child process with fd 4 opened.\n", argv[0]);
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

	// set up our SIGSYS handler; any disallowed syscalls are trapped by this handler
	// and sent up to the parent to process.
	struct sigaction sa;
	sigset_t mask;
	// Allow other signals to interrupt our signal handler.
	// If we're debugging, we also allow the signal handler to signal itself if it calls an invalid syscall.
	// This means that if the handler tries to make an invalid syscall, it will recurse until it
	// fills up the stack space and crashes.
	sigemptyset(&mask);
	sa.sa_sigaction = sigsys_handler;
	sa.sa_mask = mask;
#ifdef SB_DEBUG
	sa.sa_flags = SA_SIGINFO | SA_NODEFER;
#else
	sa.sa_flags = SA_SIGINFO;
#endif
	sigaction(SIGSYS, &sa, NULL);

	/* set up seccomp, we allow the following syscalls:
	 * IO:
	 * - read(), readv() - fd 3 only
	 * - write(), writev() - fd 4 only (also stdout and stderr if in debug mode)
	 * - fstat(), fcntl(F_GETFD), fcntl(F_GETFL) - fds 3 and 4 only
	 * Memory:
	 * - mmap(NULL, MAP_ANONYMOUS) - new mappings that don't read from fds, kernel chooses address
	 * - brk() - memory allocation (we have setrlimit to keep this in check)
	 * - munmap() - deallocation
	 * Signal Handlers:
	 * - sigreturn(), rt_sigreturn(), rt_sigprocmask(), sigaltstack()
	 * - rt_sigaction() - can retrieve all signals (2nd param NULL), cannot set handler for SIGSYS
	 * Misc:
	 * - tgkill() - Called by Python internals
	 * - futex() - dlsym() needs this, python threading probably does too
	 * - exit(), exit_group() - so program can terminate
	 */
	ctx = seccomp_init(SCMP_ACT_TRAP);

	if (ctx == NULL)
		goto cleanup;

	ret = seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(read), 1, SCMP_A0(SCMP_CMP_EQ, PIPEIN));
	if (ret < 0)
		goto cleanup;

	ret = seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(write), 1, SCMP_A0(SCMP_CMP_EQ, PIPEOUT));
	if (ret < 0)
		goto cleanup;

	ret = seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(readv), 1, SCMP_A0(SCMP_CMP_EQ, PIPEIN));
	if (ret < 0)
		goto cleanup;

	ret = seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(writev), 1, SCMP_A0(SCMP_CMP_EQ, PIPEOUT));
	if (ret < 0)
		goto cleanup;

	ret = seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(fstat), 1, SCMP_A0(SCMP_CMP_EQ, PIPEIN));
	if (ret < 0)
		goto cleanup;

	ret = seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(fstat), 1, SCMP_A0(SCMP_CMP_EQ, PIPEOUT));
	if (ret < 0)
		goto cleanup;

	ret = seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(fcntl), 2,
			SCMP_A0(SCMP_CMP_EQ, PIPEIN), SCMP_A1(SCMP_CMP_EQ, F_GETFD));
	if (ret < 0)
		goto cleanup;

	ret = seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(fcntl), 2,
			SCMP_A0(SCMP_CMP_EQ, PIPEOUT), SCMP_A1(SCMP_CMP_EQ, F_GETFD));
	if (ret < 0)
		goto cleanup;

	ret = seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(fcntl), 2,
			SCMP_A0(SCMP_CMP_EQ, PIPEIN), SCMP_A1(SCMP_CMP_EQ, F_GETFL));
	if (ret < 0)
		goto cleanup;

	ret = seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(fcntl), 2,
			SCMP_A0(SCMP_CMP_EQ, PIPEOUT), SCMP_A1(SCMP_CMP_EQ, F_GETFL));
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

	ret = seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(writev), 1, SCMP_A0(SCMP_CMP_EQ, 1));
	if (ret < 0)
		goto cleanup;

	ret = seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(writev), 1, SCMP_A0(SCMP_CMP_EQ, 2));
	if (ret < 0)
		goto cleanup;	
#endif

	ret = seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(mmap), 2,
			SCMP_A0(SCMP_CMP_EQ, NULL), SCMP_A3(SCMP_CMP_MASKED_EQ, MAP_ANONYMOUS, MAP_ANONYMOUS));
	if (ret < 0)
		goto cleanup;

	ret = seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(brk), 0);
	if (ret < 0)
		goto cleanup;

	ret = seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(munmap), 0);
	if (ret < 0)
		goto cleanup;

	ret = seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(sigreturn), 0);
	if (ret < 0)
		goto cleanup;

	ret = seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(rt_sigreturn), 0);
	if (ret < 0)
		goto cleanup;

	ret = seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(rt_sigprocmask), 0);
	if (ret < 0)
		goto cleanup;

	ret = seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(sigaltstack), 0);
	if (ret < 0)
		goto cleanup;

	ret = seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(rt_sigaction), 1, SCMP_A0(SCMP_CMP_NE, SIGSYS));
	if (ret < 0)
		goto cleanup;

	ret = seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(rt_sigaction), 1, SCMP_A1(SCMP_CMP_EQ, NULL));
	if (ret < 0)
		goto cleanup;

	ret = seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(futex), 0);
	if (ret < 0)
		goto cleanup;

	ret = seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(tgkill), 1, SCMP_A0(SCMP_CMP_EQ, getpid()));
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

	// inform the preloader that we are now inside the sandbox
	// this causes it to override a couple more libc functions that it simply passes through above
	void (*enable_sandbox)(void) = dlsym(RTLD_DEFAULT, "enable_sandbox");
	if (!enable_sandbox) {
		ret = -1;
		fprintf(stderr, "%s: Unable to find enable_sandbox function. Ensure preloader is installed.\n", argv[0]);
		goto cleanup;
	}

	enable_sandbox();

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
	// We expect that the parent proc provides a file which contains
	// this initialization code, as well as the code to run whatever the user wanted.
	mainpy = fopen("/lib/sandbox/init.py", "r");
	if (mainpy == NULL) {
		ret = errno;
		fprintf(stderr, "%s: Cannot open /lib/sandbox/init.py.\n", argv[0]);
		goto cleanup;
	}

	// this closes mainpy after completion so we don't need to fclose it in cleanup
	ret = PyRun_SimpleFile(mainpy, "init.py");
	if (ret != 0) {
		fprintf(stderr, "%s: init.py returned error %d.\n", argv[0], ret);
		goto cleanup;
	}

	// now run the user's code
	mainpy = fopen("/tmp/main.py", "r");
	if (mainpy == NULL) {
		ret = errno;
		fprintf(stderr, "%s: Cannot open /tmp/main.py.\n", argv[0]);
		goto cleanup;
	}

	ret = PyRun_SimpleFile(mainpy, "main.py");

cleanup:
	Py_Finalize();
	if (program != NULL)
		free(program);
	seccomp_release(ctx);

	return -ret;
}

void sigsys_handler(int signal, siginfo_t *siginfo, void *void_ctx)
{
	const char *name;
	int found = 0, i;
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
		SB_RET(ctx) = dispatch(arg_map[i].func, SB_P1(ctx), SB_P2(ctx), SB_P3(ctx), SB_P4(ctx), SB_P5(ctx), SB_P6(ctx));
		return;
	}

	fprintf(stderr, "Syscall not implemented %s(%lld, %lld, %lld, %lld, %lld, %lld)\n",
		name, SB_P1(ctx), SB_P2(ctx), SB_P3(ctx), SB_P4(ctx), SB_P5(ctx), SB_P6(ctx));

#ifdef SB_DEBUG	
	fputs("Backtrace:\n", stderr);
	void *buffer[32];
	int n = backtrace(buffer, 50);
	backtrace_symbols_fd(buffer, n, STDERR_FILENO);
#endif

	exit(-SIGSYS);
}
