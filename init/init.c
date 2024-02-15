#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>

int main(void) {
    setenv("TERM", "linux", 1);
    setenv("USER", "root", 1);
    setenv("HOME", "/root", 1);
    setenv("PATH", "/usr/local/bin:/usr/bin", 1);

    for (;;) {
        if (access("/usr/bin/bash", X_OK) == -1) {
            perror("init: /usr/bin/bash");
            return EXIT_FAILURE;
        }

        printf("\nWelcome to Lyre!\n");
        printf("You can find the source code at https://github.com/lyre-os/lyre\n\n");

        int pid = fork();

        if (pid == -1) 
        {
            panic("init: fork failed");
            return EXIT_FAILURE;
        }

        if (pid == 0) {
            char *argv[] = {"/usr/bin/bash", "-l", NULL};

            chdir(getenv("HOME"));
            execvp("/usr/bin/bash", argv);
            return EXIT_FAILURE;
        } else {
            int status;
            waitpid(pid, &status, 0);
        }
    }

    return EXIT_SUCCESS;
}
