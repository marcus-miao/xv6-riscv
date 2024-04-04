#include <stdbool.h>

#include "kernel/types.h"
#include "user/user.h"


#define FIRST_PRIME 2
#define PRIME_SEARCH_END 35


/// @brief Child process.
/// File descriptor 0 will refer to the read side of the pipe that his
/// parent creates.
///
/// First read an int from the parent pipe and prints out as the current prime
/// number. Next peek if there are any remaining integers to process. If none,
/// exit. Else, create a new pipe for communicating with grandchild. Write the
/// prime candidates to the pipe and recurse.
///
/// @return 0 if success. 1 on error.
__attribute__((noreturn))
void child() {
  int n;
  
  read(0, &n, sizeof(int));
  printf("prime %d\n", n);

  int i;
  if (read(0, &i, sizeof(int)) == 0) {
    exit(0);
  }

  // create pipe
  int p[2];
  if (pipe(p) < 0) {
    fprintf(2, "pipe\n");
    exit(1);
  }

  int pid = fork();
  if (pid < 0) {
    fprintf(2, "fork\n");
    exit(1);
  } else if (pid == 0) {
    close(0);
    dup(p[0]);  
    close(p[0]);
    close(p[1]); // IMPORTANT for child to close the write end of a parent pipe
    child();
  } else {
    while (true) {
      if (i % n != 0) {
        write(p[1], &i, sizeof(int));
      }
      if (read(0, &i, sizeof(int)) == 0) {
        break;
      }
    }
    close(p[1]);
    wait((int *) 0);
  }
  exit(0);
}


int main(int argc, char *argv[]) {
  int n = FIRST_PRIME;
  printf("prime %d\n", n);

  // create pipe
  int p[2];
  if (pipe(p) < 0) {
    fprintf(2, "pipe\n");
    exit(1);
  }

  int pid = fork();
  if (pid < 0) {
    fprintf(2, "fork\n");
    exit(1);
  } else if (pid == 0) {
    close(0);
    dup(p[0]);
    close(p[0]);
    close(p[1]); // IMPORTANT for child to close the write end of a parent pipe
    child();
  } else {
    // parent process
    // kick start
    close(p[0]);
    for (int i = n + 1; i < PRIME_SEARCH_END; i++) {
      if (i % n == 0) {
        continue;
      }
      write(p[1], &i, sizeof(int));
    }
    close(p[1]);
    wait((int *) 0);
  }
  exit(0);
}
