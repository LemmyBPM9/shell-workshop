#include <stdio.h> /* Standard input/output library for printing prompts and errors */
#include <stdlib.h> /* Standard library for memory allocation (calloc) and utilities */
#include <string.h> /* String manipulation library */
#include <sys/wait.h> /* Library containing process waiting functions like wait() */
#include <unistd.h> /* UNIX standard API library for fork(), pipe(), dup2(), close(), execvp() */

#include "utils.h" /* Custom helper header for command/pipeline parsing structs and functions */

/* Function to print a prompt to standard error and read a line of input from
 * stdin */
ssize_t prompt_and_get_input(const char *prompt, char **line, size_t *len) {
  /* Write prompt string to stderr instead of stdout so standard output
   * redirects remain clean */
  fputs(prompt, stderr);
  /* Read an entire line from standard input stream, dynamically allocating or
   * resizing *line as needed */
  return getline(line, len, stdin);
}

/* Closes both read [0] and write [1] file descriptors for all pipes allocated
 * in the pipes array */
void close_ALL_the_pipes(int n_pipes, int (*pipes)[2]) {
  /* Loop through each pipe index from 0 to n_pipes - 1 */
  for (int i = 0; i < n_pipes; ++i) {
    /* Close the read end of pipe i */
    close(pipes[i][0]);
    /* Close the write end of pipe i */
    close(pipes[i][1]);
  }
}

/* Performs redirection for stdin and stdout in the child process, closes unused
 * pipes, and executes the command */
int exec_with_redir(cmd_struct *command, int n_pipes, int (*pipes)[2]) {
  /* Temporary integer variable to hold file descriptors to redirect */
  int fd = -1;
  /* If redirect[0] (stdin redirection) is configured (not -1) */
  if ((fd = command->redirect[0]) != -1) {
    /* Duplicate fd to STDIN_FILENO (0) so the process reads from this
     * descriptor as stdin */
    dup2(fd, STDIN_FILENO);
  }
  /* If redirect[1] (stdout redirection) is configured (not -1) */
  if ((fd = command->redirect[1]) != -1) {
    /* Duplicate fd to STDOUT_FILENO (1) so the process writes to this
     * descriptor as stdout */
    dup2(fd, STDOUT_FILENO);
  }
  /* Close all pipe file descriptors in this child process to avoid hangs or
   * descriptor leaks */
  close_ALL_the_pipes(n_pipes, pipes);
  /* Replace the current child process image with the new program specified by
   * progname and args */
  return execvp(command->progname, command->args);
}

/* Forks a new child process and initiates the redirection and execution steps
 * inside the child */
pid_t run_with_redir(cmd_struct *command, int n_pipes, int (*pipes)[2]) {
  /* Duplicate the current process; child_pid will be 0 in the child, and the
   * child's PID in the parent */
  pid_t child_pid = fork();

  /* Check if we are in the parent process (non-zero child_pid) or if fork
   * failed (-1) */
  if (child_pid) { /* We are the parent. */
    /* Inspect the fork return value to distinguish error from success */
    switch (child_pid) {
    /* If fork return value is -1, an error occurred while spawning the process
     */
    case -1:
      /* Output error message to standard error stream */
      fprintf(stderr, "Oh dear.\n");
      /* Return failure status back to the caller */
      return -1;
    /* If fork return value is positive, it is the spawned child's process ID */
    default:
      /* Return the child's process ID to the parent caller */
      return child_pid;
    }
  } else { // We are the child. */
    /* Redirect standard streams as configured and launch the executable */
    exec_with_redir(command, n_pipes, pipes);
    /* Print system error message if exec_with_redir returns (which only happens
     * on failure) */
    perror("OH DEAR");
    /* Return success status code, though typically a failed child should exit
     * directly */
    return 0;
  }
}

/* The main entry point of the shell program */
int main(void) {
  /* Pointer to hold the dynamically allocated buffer for the command line input
   */
  char *line = NULL;
  /* Size of the allocated buffer for the command line input */
  size_t len = 0;

  /* Prompt loop: read user input from shell prompt line-by-line until
   * end-of-file (Ctrl+D) or error */
  while (prompt_and_get_input("heeee> ", &line, &len) > 0) {
    /* Parse the input line into a pipeline structure containing individual
     * commands split by '|' */
    pipeline_struct *pipeline = parse_pipeline(line);
    /* Calculate the number of pipe descriptors needed (number of commands minus
     * 1) */
    int n_pipes = pipeline->n_cmds - 1;
    // print_pipeline(pipeline);

    /* Allocate memory for an array of file descriptor pairs (pipes),
     * zero-initialized */
    int (*pipes)[2] = calloc(sizeof(int[2]), n_pipes);

    /* Set up redirects between adjacent commands in the pipeline using pipes */
    for (int i = 1; i < pipeline->n_cmds; ++i) {
      /* Create a new system pipe for this boundary and store read/write fds in
       * pipes[i-1] */
      pipe(pipes[i - 1]);
      /* Set the read end of the pipe as the standard input redirection for the
       * downstream command */
      pipeline->cmds[i]->redirect[STDIN_FILENO] = pipes[i - 1][0];
      /* Set the write end of the pipe as the standard output redirection for
       * the upstream command */
      pipeline->cmds[i - 1]->redirect[STDOUT_FILENO] = pipes[i - 1][1];
    }

    /* Spawn and execute each command in the pipeline */
    for (int i = 0; i < pipeline->n_cmds; ++i) {
      /* Fork and execute command i with its mapped redirects */
      run_with_redir(pipeline->cmds[i], n_pipes, pipes);
    }

    /* Close the parent's copies of all pipe fds to trigger EOF on pipe readers
     * and release resources */
    close_ALL_the_pipes(n_pipes, pipes);

    /* Wait for all child processes spawned in the pipeline loop to finish
     * execution */
    for (int i = 0; i < pipeline->n_cmds; ++i) {
      /* Block execution until a child process terminates */
      wait(NULL);
    }
  }
  /* Print a newline upon exiting the prompt loop (normally due to EOF/Ctrl+D)
   */
  fputs("\n", stderr);
  /* Exit shell program successfully */
  return 0;
}
