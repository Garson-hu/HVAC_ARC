#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <fcntl.h>
#include <execinfo.h>
#include <stdarg.h>
// 定义一个指向原始 open 的函数指针
static int (*real_open)(const char *pathname, int flags, ...) = NULL;

int open(const char *pathname, int flags, ...) {
    // 初始化原始 open 函数
    if (!real_open) {
        real_open = dlsym(RTLD_NEXT, "open");
    }

    // 打印文件路径
    printf("File opened: %s\n", pathname);

    // 打印调用栈
    void *buffer[10];
    int nptrs = backtrace(buffer, 10);
    backtrace_symbols_fd(buffer, nptrs, fileno(stdout));

    // 调用原始的 open 函数
    if (flags & O_CREAT) {
        va_list args;
        va_start(args, flags);
        int mode = va_arg(args, int);
        va_end(args);
        return real_open(pathname, flags, mode);
    } else {
        return real_open(pathname, flags);
    }
}
