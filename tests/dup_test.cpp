#ifndef _GNU_SOURCE
    #define _GNU_SOURCE
#endif
#include <stdio.h>
#include <dlfcn.h>
#include <stdarg.h>
#include <fcntl.h>
#include <time.h>

extern "C" {
    // Define function pointers for the real dup and dup2
    int (*real_dup)(int oldfd) = nullptr;
    int (*real_dup2)(int oldfd, int newfd) = nullptr;

    int dup(int oldfd) {
        if (!real_dup) {
            real_dup = (int (*)(int)) dlsym(RTLD_NEXT, "dup");
            if (!real_dup) {
                fprintf(stderr, "Error in `dlsym` for dup\n");
                return -1;
            }
        }

        int newfd = real_dup(oldfd);
        printf("DEBUG_HU: MY_LD_PRELOAD: Dup oldfd %d to newfd %d\n", oldfd, newfd);
        return newfd;
    }

    int dup2(int oldfd, int newfd) {
        if (!real_dup2) {
            real_dup2 = (int (*)(int, int)) dlsym(RTLD_NEXT, "dup2");
            if (!real_dup2) {
                fprintf(stderr, "Error in `dlsym` for dup2\n");
                return -1;
            }
        }

        int retfd = real_dup2(oldfd, newfd);
        printf("DEBUG_HU: MY_LD_PRELOAD: Dup2 oldfd %d to newfd %d\n", oldfd, newfd);
        return retfd;
    }
}
