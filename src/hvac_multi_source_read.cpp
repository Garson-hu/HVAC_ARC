#include <pthread.h>
#include <cstdio>
#include <cstdlib>
#include <cassert>
#include <mutex>
#include <condition_variable>

#include "hvac_multi_source_read.hpp"
#include "hvac_comm.h"            // For hvac_client_comm_gen_read_rpc, etc.
#include "hvac_internal.h"        // For hvac_file_tracked, hvac_get_path, etc.
#include "hvac_logging.h"         // For L4C_INFO, L4C_ERR
#include "wrappers.h"             // Possibly needed for MAP_OR_FAIL


pthread_mutex_t      lock;
pthread_cond_t       cond;


// A structure to hold the asynchronous state for the multi-source read
struct ms_read_state {
    bool                 completed;    // Have we returned data to the user?
    bool                 pm_done;      // Has PM request finished?
    bool                 ssd_done;     // Has SSD request finished?
    ssize_t              pm_result;    // Bytes read from PM
    ssize_t              ssd_result;   // Bytes read from SSD
    void*                user_buf;     // Destination buffer
    size_t               count;        // Byte count to read
    off_t                offset;       // -1 if normal read, else pread offset
};


int get_PM_rank() 
{
    // TODO: Implement this function
}

ssize_t hvac::ms_read(int fd, void* buf, size_t count, off_t offset) 
{
    
    // 1. Check if file is tracked, otherwise fallback to normal read
    if (!hvac_file_tracked(fd)) 
    {
        MAP_OR_FAIL(read);
        L4C_INFO("File not tracked, falling back to normal read");
        if(offset == -1) 
        {
            return __real_read(fd, buf, count);
        }
        else 
        {
            MAP_OR_FAIL(pread);
            return __real_pread(fd, buf, count, offset);
        }
    }

    // 2. Create ms_read_state to store asynchronous results
    ms_read_state state;
    pthread_mutex_init(&state.lock, nullptr);
    pthread_cond_init(&state.cond, nullptr);
    state.completed = false;
    state.pm_done = false;
    state.ssd_done = false;
    state.pm_result = -1;
    state.ssd_result = -1;
    state.user_buf = buf;
    state.count = count;
    state.offset = offset;

    // 3. Determine the "remote fd" or server rank
    // map the local fd to the remote fd, which will be used by the RPC
    int remote_fd = fd_redir_map[fd];
    
    // Decide the rank of PM (since not all nodes have PM)
    int pm_rank = get_PM_rank();

    int ssd_rank = get_SSD_rank();

    // 4. Launch PM read RPC
    hvac_client_comm_gen_read_rpc(pm_rank, remote_fd, buf, count, offset,
                                pm_read_cb, &state, Tier_PM);

    // 5. Launch SSD read RPC
    hvac_client_comm_gen_read_rpc(ssd_rank, remote_fd, buf, count, offset,
                                ssd_read_cb, &state, Tier_SSD);

    // 6. Wait for one request to succeed
    pthread_mutex_lock(&lock);
    while(!state.completed && (!state.pm_done || !state.ssd_done)) 
    {
        pthread_cond_wait(&cond, &lock);
    }
    bool done = state.completed;
    bool pm_res = state.pm_done;
    bool ssd_res = state.ssd_done;
    pthread_mutex_unlock(&lock);

    // 7. Clean up

    // 8. Return whichever result completed first

    return 0;
}