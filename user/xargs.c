#include "kernel/types.h"
#include "kernel/param.h"
#include "user/user.h"

#define CHUNK_SIZE_BYTES 64

void runcmd(char *path, char *argv[]) {
  int pid = fork();
  if (pid == 0) {
    exec(path, argv);
    exit(0);
  } else if (pid < 0) {
    fprintf(2, "xargs: fork failure\n");
    exit(1);
  } else {
    wait((int *) 0);
  }
}

int main(int argc, char *argv[]) {
  if (argc < 2) {
    fprintf(2, "Usage: xargs [cmd] [arg1] [arg2]...\n");
    exit(1);
  }

  // dynamic argument size.
  uint arg_size = CHUNK_SIZE_BYTES;
  char *arg = malloc(arg_size);

  // <path> is the first arg for exec function call.
  // For example, if the command is xargs echo hello, path will be "/echo"
  uint path_len = strlen(argv[1]) + 1 + 1;
  char *path = malloc(path_len);
  strcpy(path + 1, argv[1]);
  path[0] = '/';
  path[path_len - 1] = 0;

  // new_argv is the second arg for exec function call.
  // Different from argv, new_argv starts from the second element of argv since
  // the first argument will be xargs.
  // For example, if argv = {"xargs", "echo", "hello"}, then the initial new_argv
  // will be {"echo", hello}
  char *new_argv[MAXARG];
  memcpy(new_argv, argv + 1, (argc - 1) * sizeof(char *));

  char c;
  uint char_idx = 0;
  uint arg_idx = argc - 1;
  while (read(0, &c, sizeof(char)) == sizeof(char)) {

    // dynamically resize the heap memory allocated for the current argument
    if (char_idx >= arg_size) {
      uint new_arg_size = arg_size + CHUNK_SIZE_BYTES;
      char *tmp = malloc(new_arg_size);
      memcpy(tmp, arg, arg_size);
      free(arg);

      arg = tmp;
      arg_size = new_arg_size;
    }

    if (c == ' ' || c == '\n' || c == 0) {
      arg[char_idx] = 0;
      if (arg_idx >= MAXARG) {
        fprintf(2, "xargs: too many arguments %d\n", arg_idx);
        exit(1);
      }
      new_argv[arg_idx] = arg;
      arg_idx++;

      if (c != ' ') {
        runcmd(path, new_argv);
        for (int i = argc - 1; i < arg_idx; i++) {
          free(new_argv[i]);
        }
        arg_idx = argc - 1;
        memset(new_argv, 0, MAXARG);
        memcpy(new_argv, argv + 1, (argc - 1) * sizeof(char *));
      }
      
      arg_size = CHUNK_SIZE_BYTES;
      arg = malloc(arg_size);
      char_idx = 0;
    } else {
      arg[char_idx] = c;
      char_idx++;
    }
  }

  free(path);
  exit(0);
}
