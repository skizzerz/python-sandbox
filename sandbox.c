#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>

#include "sbcontext.h"
#include "sblibc.h"

bool is_child;

void sigchld_handler(int sig)
{
	int status;

	if (is_child) {
		// ensure we don't leave around zombies, but otherwise do nothing
		while (waitpid(-1, NULL, WNOHANG) > 0);
		return;
	}

	while (waitpid(-1, &status, WNOHANG) > 0) {
		if (WIFEXITED(status)) {
			exit(WEXITSTATUS(status));
		}

		if (WIFSIGNALED(status)) {
			exit(-(WTERMSIG(status)));
		}

		// if we get here then the child wasn't actually terminated,
		// so continue execution. This is in a loop just in case the child
		// manages to set is_child back to false, so that it cannot leave
		// zombie processes lying around; in normal execution there is only
		// one child here to worry about.
	}
}

int main(int argc, char *argv[])
{
	int ret, sv[2];
	uid_t ruid, euid, suid;
	gid_t rgid, egid, sgid;
	pid_t pid;

	/* verify that fds 3 and 4 have been opened for us */
	ret = fcntl(PIPEIN, F_GETFD);
	if (ret < 0) {
		fprintf(stderr, "%s: Must be run as a child process with fd 3 opened.\n", argv[0]);
		return 1;
	}
	
	ret = fcntl(PIPEOUT, F_GETFD);
	if (ret < 0) {
		fprintf(stderr, "%s: Must be run as a child process with fd 4 opened.\n", argv[0]);
		return 1;
	}

	/* check privileges; we refuse to run as root. If called in a setuid/setgid context,
	 * we switch entirely to that uid/gid without a means of switching back to the original user.
	 */
	ret = getresuid(&ruid, &euid, &suid);
	if (ret < 0) {
		fprintf(stderr, "%s: Error with getresuid: %s.\n", argv[0], strerror(errno));
		return -errno;
	}

	ret = getresgid(&rgid, &egid, &sgid);
	if (ret < 0) {
		fprintf(stderr, "%s: Error with getresgid: %s.\n", argv[0], strerror(errno));
		return -errno;
	}

	if (euid == 0 || egid == 0) {
		fprintf(stderr, "%s: Cannot be run as root or setuid/setgid root.\n", argv[0]);
		return 1;
	}

	ret = setresuid(euid, euid, euid);
	if (ret < 0) {
		fprintf(stderr, "%s: Error with setresuid: %s.\n", argv[0], strerror(errno));
		return -errno;
	}

	ret = setresgid(egid, egid, egid);
	if (ret < 0) {
		fprintf(stderr, "%s: Error with setresgid: %s.\n", argv[0], strerror(errno));
		return -errno;
	}

	/* create a new socketpair for communicating between our parent/child process (comms
	 * to the process that created us still happens on fds 3 and 4, which our child does not
	 * inherit).
	 */
	ret = socketpair(AF_UNIX, SOCK_DGRAM, 0, sv);
	if (ret < 0) {
		fprintf(stderr, "%s: Error with socketpair: %s.\n", argv[0], strerror(errno));
		return -errno;
	}

	/* register our SIGCHLD handler pre-fork just in case the child exits before the handler
	 * can get set up in the parent. The child really has no use for the handler.
	 */
	struct sigaction sa;
	memset(&sa, 0, sizeof(struct sigaction));
	sa.sa_handler = sigchld_handler;
	sa.sa_flags = SA_NOCLDSTOP;
	ret = sigaction(SIGCHLD, &sa, NULL);
	if (ret < 0) {
		fprintf(stderr, "%s: Error with sigaction: %s.\n", argv[0], strerror(errno));
		return -errno;
	}

	/* split into a parent and child with fork(); the parent is responsible for handling
	 * most of the virtualization (only talking with the parent's parent when needed) and
	 * the child is what is running the actual sandbox.
	 */
	pid = fork();
	if (pid < 0) {
		fprintf(stderr, "%s: Error with fork: %s.\n", argv[0], strerror(errno));
		return -errno;
	}

	if (pid == 0) {
		/* child */
		ret = close(sv[0]);
		if (ret < 0) {
			fprintf(stderr, "%s: Error closing socket: %s.\n", argv[0], strerror(errno));
			return -errno;
		}

		ret = close(PIPEIN);
		if (ret < 0) {
			fprintf(stderr, "%s: Error closing pipe: %s.\n", argv[0], strerror(errno));
			return -errno;
		}

		ret = close(PIPEOUT);
		if (ret < 0) {
			fprintf(stderr, "%s: Error closing pipe: %s.\n", argv[0], strerror(errno));
			return -errno;
		}

		/* reset our comm socket to our parent to fd 3 no matter what it used to be,
		 * makes less work for the parent in allocating virtual fds to us.
		 */
		if (sv[1] != 3) {
			ret = dup2(sv[1], 3);
			if (ret < 0) {
				fprintf(stderr, "%s: Error with dup2: %s.\n", argv[0], strerror(errno));
				return -errno;
			}

			close(sv[1]);
		}

		/* wipe data previously stored in these vars; it's a minor info leak if the sandbox
		 * can obtain the uid/gid that originally called our process. I'm not sure what it
		 * can actually *do* with that info, but there's no reason to grant access to it.
		 */
		ruid = suid = euid = -1;
		rgid = sgid = egid = -1;

		is_child = true;
		ret = run_child();
	} else {
		/* parent */
		close(sv[1]);
		is_child = false;
		ret = run_parent(pid, sv[0]);
	}

	return ret;
}
