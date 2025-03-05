#ifndef HVAC_MULTI_SOURCE_READ_H
#define HVAC_MULTI_SOURCE_READ_H

#ifdef __cplusplus
    #include <cstddef>
    #include <mutex>
extern "C" {
#endif
#include <pthread.h>
#include <stddef.h>   
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>

#ifdef __cplusplus
} 
namespace hvac {
#endif

ssize_t ms_read(int fd, void* buf, size_t count, off_t offset);

#ifdef __cplusplus
}  // namespace hvac
#endif


// A structure to hold the asynchronous state for the multi-source read
struct ms_read_state {
    pthread_mutex_t      lock;
    pthread_cond_t       cond;
    bool                 completed;    // Have we returned data to the user?
    bool                 pm_done;      // Has PM request finished?
    bool                 ssd_done;     // Has SSD request finished?
    ssize_t              pm_result;    // Bytes read from PM
    ssize_t              ssd_result;   // Bytes read from SSD
};

#endif  // HVAC_MULTI_SOURCE_READ_H




