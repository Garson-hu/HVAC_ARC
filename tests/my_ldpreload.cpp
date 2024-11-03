#ifndef _GNU_SOURCE
    #define _GNU_SOURCE
#endif
#include <iostream>
#include <stdio.h>
#include <dlfcn.h>
#include <stdarg.h>
#include <fcntl.h>
#include <time.h>
#include <vector>
#include <set>
#include <fstream>

extern "C" {
    int (*real_open)(const char *pathname, int flags, ...) = nullptr;
    int (*real_open64)(const char *pathname, int flags, ...) = nullptr;
    FILE* (*real_fopen)(const char *pathname, const char *mode) = nullptr;
    ssize_t (*real_read)(int fd, void *buf, size_t count) = nullptr;
    int (*real_close)(int fd) = nullptr;
    // int (*real_openat)(int dirfd, const char *pathname, int flags, ...) = nullptr;
    // int (*real_creat)(const char *pathname, mode_t mode) = nullptr;
    // FILE* (*real_fdopen)(int fd, const char *mode) = nullptr;
    // FILE* (*real_freopen)(const char *pathname, const char *mode, FILE *stream) = nullptr;


    size_t open_count = 0;
    size_t fopen_count = 0;
    size_t read_count = 0;
    size_t close_count = 0;

    size_t openat_count = 0;
    size_t creat_count = 0;
    size_t fdopen_count = 0;
    size_t freopen_count = 0;

    std::vector<long> open_times;
    std::vector<long> fopen_times;
    std::vector<long> read_times;
    std::vector<long> close_times;

    std::set<int> unique_open_fds;
    std::set<int> unique_close_fds;
    std::set<FILE*> unique_fopen_files;
    std::set<int> unique_openat_fds;
    std::set<int> unique_creat_fds;
    std::set<FILE*> unique_fdopen_files;
    std::set<FILE*> unique_freopen_files;
    long get_duration(struct timespec start, struct timespec end) {
        return (end.tv_sec - start.tv_sec) * 1e9 + (end.tv_nsec - start.tv_nsec);
    }

    int open(const char *pathname, int flags, ...) {
        struct timespec start, end;
        clock_gettime(CLOCK_MONOTONIC, &start);

        if (!real_open) {
            real_open = (int (*)(const char*, int, ...)) dlsym(RTLD_NEXT, "open");
            if (!real_open) {
                fprintf(stderr, "Error in `dlsym` for open\n");
                return -1;
            }
        }

        va_list args;
        va_start(args, flags);
        int fd;
        if (flags & O_CREAT) {
            mode_t mode = va_arg(args, mode_t);
            fd = real_open(pathname, flags, mode);
        } else {
            fd = real_open(pathname, flags);
        }
        va_end(args);

        clock_gettime(CLOCK_MONOTONIC, &end);
        open_times.push_back(get_duration(start, end));
        printf("DEBUG_HU: MY_LD_PRELOAD: Open fd %d from pathname %s\n", fd, pathname);

        if (fd >= 0) {
            unique_open_fds.insert(fd);
            open_count++;
        } else {
            printf("DEBUG_HU: MY_LD_PRELOAD: Failed to open %s\n", pathname);
        }
        return fd;
    }

    int open64(const char *pathname, int flags, ...) {
        if (!real_open64) {
            real_open64 = (int (*)(const char *, int, ...)) dlsym(RTLD_NEXT, "open64");
        }

        va_list args;
        va_start(args, flags);
        int fd;
        if (flags & O_CREAT) {
            mode_t mode = va_arg(args, mode_t);
            fd = real_open64(pathname, flags, mode);
        } else {
            fd = real_open64(pathname, flags);
        }
        va_end(args);

        printf("DEBUG_HU: MY_LD_PRELOAD: Open64 fd %d from pathname %s\n", fd, pathname);
        return fd;
    }

    FILE* fopen(const char *pathname, const char *mode) {
        struct timespec start, end;
        clock_gettime(CLOCK_MONOTONIC, &start);

        if (!real_fopen) {
            real_fopen = (FILE* (*)(const char*, const char*)) dlsym(RTLD_NEXT, "fopen");
            if (!real_fopen) {
                fprintf(stderr, "Error in `dlsym` for fopen\n");
                return nullptr;
            }
        }

        FILE* file = real_fopen(pathname, mode);

        clock_gettime(CLOCK_MONOTONIC, &end);
        fopen_times.push_back(get_duration(start, end));
        printf("DEBUG_HU: MY_LD_PRELOAD: Fopen file %p from pathname %s\n", file, pathname);

        if (file) {
            unique_fopen_files.insert(file);
            fopen_count++;
        } else {
            printf("DEBUG_HU: MY_LD_PRELOAD: Failed to fopen %s\n", pathname);
        }
        return file;
    }

    ssize_t read(int fd, void *buf, size_t count) {
        struct timespec start, end;
        clock_gettime(CLOCK_MONOTONIC, &start);

        if (!real_read) {
            real_read = (ssize_t (*)(int, void*, size_t)) dlsym(RTLD_NEXT, "read");
            if (!real_read) {
                fprintf(stderr, "Error in `dlsym` for read\n");
                return -1;
            }
        }
        ssize_t ret = real_read(fd, buf, count);

        clock_gettime(CLOCK_MONOTONIC, &end);
        read_times.push_back(get_duration(start, end));
        read_count++;
        return ret;
    }
    // !--------------------------------- Maybe unrelated function --------------------------------
    // int openat(int dirfd, const char *pathname, int flags, ...) {
    //     if (!real_openat) {
    //         real_openat = (int (*)(int, const char*, int, ...)) dlsym(RTLD_NEXT, "openat");
    //     }

    //     va_list args;
    //     va_start(args, flags);
    //     int fd;
    //     if (flags & O_CREAT) {
    //         mode_t mode = va_arg(args, mode_t);
    //         fd = real_openat(dirfd, pathname, flags, mode);
    //     } else {
    //         fd = real_openat(dirfd, pathname, flags);
    //     }
    //     va_end(args);

    //     printf("DEBUG_HU: MY_LD_PRELOAD: Openat fd %d from pathname %s\n", fd, pathname);

    //     if (fd >= 0) {
    //         unique_openat_fds.insert(fd);
    //         openat_count++;
    //     }
    //     return fd;
    // }

    // // Creat
    // int creat(const char *pathname, mode_t mode) {
    //     if (!real_creat) {
    //         real_creat = (int (*)(const char*, mode_t)) dlsym(RTLD_NEXT, "creat");
    //     }

    //     int fd = real_creat(pathname, mode);
    //     printf("DEBUG_HU: MY_LD_PRELOAD: Creat fd %d from pathname %s\n", fd, pathname);

    //     if (fd >= 0) {
    //         unique_creat_fds.insert(fd);
    //         creat_count++;
    //     }
    //     return fd;
    // }

    // // Fdopen
    // FILE* fdopen(int fd, const char *mode) {
    //     if (!real_fdopen) {
    //         real_fdopen = (FILE* (*)(int, const char*)) dlsym(RTLD_NEXT, "fdopen");
    //     }

    //     FILE* file = real_fdopen(fd, mode);
    //     printf("DEBUG_HU: MY_LD_PRELOAD: Fdopen file %p for fd %d\n", file, fd);

    //     if (file) {
    //         unique_fdopen_files.insert(file);
    //         fdopen_count++;
    //     }
    //     return file;
    // }

    // // Freopen
    // FILE* freopen(const char *pathname, const char *mode, FILE *stream) {
    //     if (!real_freopen) {
    //         real_freopen = (FILE* (*)(const char*, const char*, FILE*)) dlsym(RTLD_NEXT, "freopen");
    //     }

    //     FILE* file = real_freopen(pathname, mode, stream);
    //     printf("DEBUG_HU: MY_LD_PRELOAD: Freopen file %p to pathname %s\n", file, pathname);

    //     if (file) {
    //         unique_freopen_files.insert(file);
    //         freopen_count++;
    //     }
    //     return file;
    // }
    //  !---------------------------------  Maybe unrelated function --------------------------------

    int close(int fd) {
        struct timespec start, end;
        clock_gettime(CLOCK_MONOTONIC, &start);

        if (!real_close) {
            real_close = (int (*)(int)) dlsym(RTLD_NEXT, "close");
            if (!real_close) {
                fprintf(stderr, "Error in `dlsym` for close\n");
                return -1;
            }
        }
        int ret = real_close(fd);

        clock_gettime(CLOCK_MONOTONIC, &end);
        close_times.push_back(get_duration(start, end));
        printf("DEBUG_HU: MY_LD_PRELOAD: Close fd %d\n", fd);
        close_count++;
        unique_close_fds.insert(fd);
        return ret;
    }

    __attribute__((destructor)) void write_results() {
        std::ofstream outfile("operation_stats.txt");

        outfile << "Open operations: " << open_count << "\n";
        for (auto time : open_times) outfile << time << " ns\n";

        outfile << "\nFopen operations: " << fopen_count << "\n";
        for (auto time : fopen_times) outfile << time << " ns\n";

        outfile << "\nRead operations: " << read_count << "\n";
        for (auto time : read_times) outfile << time << " ns\n";

        outfile << "\nClose operations: " << close_count << "\n";
        for (auto time : close_times) outfile << time << " ns\n";

        outfile << "\nUnique Open FDs: " << unique_open_fds.size() << ", FDs: ";
        for (auto fd : unique_open_fds) outfile << fd << " ";
        outfile << "\n";

        outfile << "Unique Close FDs: " << unique_close_fds.size() << ", FDs: ";
        for (auto fd : unique_close_fds) outfile << fd << " ";
        outfile << "\n";

        outfile << "Unique Fopen files: " << unique_fopen_files.size() << ", Files: ";
        for (auto file : unique_fopen_files) outfile << file << " ";
        outfile << "\n";

        outfile.close();
    }
}
