#include "kernel/types.h"
#include "user/user.h"

int main(int argc, char *argv[]) {
  int p[2]; // p[0] read fd, p[1] write fd
  pipe(p);

  const char msg = 'a';
  char buf[1];

  int pid;
  if (fork() == 0) {
    // child process
    pid = getpid();

    // read
    if (read(p[0], buf, 1) != 1) {
      fprintf(2, "child read failure\n");
      close(p[0]);
      close(p[1]);
      exit(1);
    }
    close(p[0]);
    printf("%d: received ping\n", pid);

    // write back
    if (write(p[1], buf, 1) != 1) {
      fprintf(2, "child write failure\n");
      close(p[1]);
      exit(1);
    }

    close(p[1]);
    exit(0);
  } else {
    // parent process
    pid = getpid();

    // write
    if (write(p[1], &msg, 1) != 1) {
      fprintf(2, "parent write failure\n");
      close(p[0]);
      close(p[1]);
      exit(1);
    }
    close(p[1]);

    wait((int *)0);
    // read
    if (read(p[0], buf, 1) != 1) {
      fprintf(2, "parent read failure\n");
      close(p[0]);
      exit(1);
    }
    close(p[0]);
    printf("%d: received pong\n", pid);
    exit(0);
  }
}
