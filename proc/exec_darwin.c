#include "exec_darwin.h"

extern char** environ;

int
fork_exec(char *argv0, char **argv, int size,
		mach_port_name_t *task,
		mach_port_t *port_set,
		mach_port_t *exception_port,
		mach_port_t *notification_port)
{
	// Create a pipe so that we know when we have exec'ed. This ensures that control only returns
	// back to Go-land when we call exec, effectively eliminating a race condition between launching the new
	// process and trying to read its memory.
	int wfd[2];
	if (pipe(wfd) < 0) return -1;
	if (fcntl(wfd[0], FD_CLOEXEC) < 0) return -1;
	if (fcntl(wfd[1], FD_CLOEXEC) < 0) return -1;

	kern_return_t kret;
	pid_t pid = fork();
	if (pid > 0) {
		// In parent.
		kret = acquire_mach_task(pid, task, port_set, exception_port, notification_port);
		if (kret != KERN_SUCCESS) return -1;

		char w;
		read(wfd[0], &w, 1);

		return pid;
	}

	// In child.
	int pret;

	// Create a new process group.
	if (setpgid(0, 0) < 0) {
		return -1;
	}

	// Set errno to zero before a call to ptrace.
	// It is documented that ptrace can return -1 even
	// for successful calls.
	errno = 0;
	pret = ptrace(PT_TRACE_ME, 0, 0, 0);
	if (pret != 0 && errno != 0) return -errno;

	errno = 0;
	pret = ptrace(PT_SIGEXC, 0, 0, 0);
	if (pret != 0 && errno != 0) return -errno;

	// Create the child process.
	execve(argv0, argv, environ);

	// We should never reach here, but if we did something went wrong.
	exit(1);
}
