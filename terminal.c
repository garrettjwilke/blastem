#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <stdint.h>
#include <signal.h>
#include "paths.h"
#include "terminal.h"

pid_t child;

void cleanup_terminal()
{
	kill(child, SIGKILL);
	unlink(INPUT_PATH);
	unlink(OUTPUT_PATH);
}

static char init_done;

void force_no_terminal()
{
	init_done = 1;
}

#if !defined(IS_LIB) && !defined(__ANDROID__)
//How long to wait for termhelper to come up before giving up on it
#define TERMHELPER_TIMEOUT_MS 3000

//Opening a FIFO for writing fails with ENXIO until a reader connects, so this
//doubles as proof that termhelper actually launched. Without the timeout a
//helper that never starts (missing from the app bundle, blocked by Gatekeeper,
//no terminal emulator installed) wedges the emulator in open() forever.
static int open_output_fifo(void)
{
	for (int waited = 0; waited < TERMHELPER_TIMEOUT_MS; waited += 50)
	{
		int fd = open(OUTPUT_PATH, O_WRONLY | O_NONBLOCK);
		if (fd >= 0) {
			int flags = fcntl(fd, F_GETFL);
			if (flags != -1) {
				fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
			}
			return fd;
		}
		if (errno != ENXIO) {
			return -1;
		}
		usleep(50000);
	}
	return -1;
}

//O_NONBLOCK keeps this from blocking on a writer that may never show up. The
//flag is cleared again immediately; by the time we actually read from it
//termhelper is known to be connected.
static int open_input_fifo(void)
{
	int fd = open(INPUT_PATH, O_RDONLY | O_NONBLOCK);
	if (fd >= 0) {
		int flags = fcntl(fd, F_GETFL);
		if (flags != -1) {
			fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
		}
	}
	return fd;
}

//Put stdin/stdout/stderr back on /dev/null after a failed handoff so later
//printf()s hit a valid (if useless) descriptor rather than a closed one
static void restore_null_stdio(void)
{
	for (int i = 0; i <= STDERR_FILENO; i++)
	{
		if (fcntl(i, F_GETFD) != -1 || errno != EBADF) {
			continue;
		}
		int fd = open("/dev/null", O_RDWR);
		if (fd < 0) {
			return;
		}
		if (fd != i) {
			close(fd);
			return;
		}
	}
}
#endif

void init_terminal()
{
#if !defined(IS_LIB) && !defined(__ANDROID__)
	if (!init_done) {
		if (!(isatty(STDIN_FILENO) && isatty(STDOUT_FILENO))) {
			struct stat st;
			char *termhelper = bundled_file_path("termhelper");
			if (!termhelper || stat(termhelper, &st)) {
				//Nothing to hand our output off to. Leave stdio alone instead of
				//closing it and then blocking on a FIFO nobody will ever open.
				//This is the normal case for the macOS .app bundle, which does
				//not ship termhelper.
				free(termhelper);
				init_done = 1;
				return;
			}
#ifndef __APPLE__
			//check to see if x-terminal-emulator exists, just use xterm if it doesn't
			char *term = system("which x-terminal-emulator > /dev/null") ? "xterm" : "x-terminal-emulator";
#endif
			//get rid of FIFO's if they already exist
			unlink(INPUT_PATH);
			unlink(OUTPUT_PATH);
			//create FIFOs for talking to helper process in terminal app
			mkfifo(INPUT_PATH, 0666);
			mkfifo(OUTPUT_PATH, 0666);

			//close existing file descriptors
			close(STDIN_FILENO);
			close(STDOUT_FILENO);
			close(STDERR_FILENO);

			child = fork();
			if (child == -1) {
				//error, oh well
				restore_null_stdio();
				warning("Failed to fork for terminal spawn");
			} else if (!child) {
				//child process, exec our terminal emulator
#ifdef __APPLE__
				execlp("open", "open", termhelper, NULL);
#else
				execlp(term, term, "-title", "BlastEm Debugger", "-e", termhelper, NULL);
#endif
				//exec failed; bail out rather than continuing as a second emulator
				_exit(1);
			} else {
				//connect to the FIFOs, order is important
				int in = open_input_fifo();
				int out = in < 0 ? -1 : open_output_fifo();
				if (out < 0) {
					//termhelper never connected, recover instead of hanging
					kill(child, SIGKILL);
					unlink(INPUT_PATH);
					unlink(OUTPUT_PATH);
					if (in >= 0) {
						close(in);
					}
					restore_null_stdio();
					free(termhelper);
					init_done = 1;
					return;
				}
				atexit(cleanup_terminal);
				if (-1 == dup(STDOUT_FILENO)) {
					fatal_error("failed to dup STDOUT to STDERR after terminal fork");
				}
			}
			free(termhelper);
		}

		init_done = 1;
	}
#endif
}
