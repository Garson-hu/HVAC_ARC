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


// A structure to hold the asynchronous state for the multi-source read
struct ms_read_state {
    pthread_mutex_t      lock;
    pthread_cond_t       cond;
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

int get_SSD_rank() 
{
    // TODO: Implement this function
}

static hg_return_t hvac_ms_read_cb(const struct hg_cb_info *info);

/*
    * Callback function for the PM read operation.
    @user_buf: The buffer to store the read data.
    @count: Number of bytes to read.
    @tier: The tier of the storage device (PM or SSD).
*/
static hg_rpc_state* create_rpc_state(void * user_buf, size_t count, cacher_tier_t tier) 
{
    hg_rpc_state* rpc_state = (hg_rpc_state*) malloc(sizeof(hg_rpc_state));
    pthread_mutex_init(&rpc_state->lock, nullptr);
    pthread_cond_init(&rpc_state->cond, nullptr);
    rpc_state->completed = false;
    rpc_state->result = -1;

    rpc_state->user_buf = user_buf;
    rpc_state->size = count;
    rpc_state->tier = tier;
    return rpc_state;
}

static void destroy_rpc_state(hg_rpc_state* rpc_state) 
{
    pthread_mutex_destroy(&rpc_state->lock);
    pthread_cond_destroy(&rpc_state->cond);
    free(rpc_state);
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
    hvac_prc_state* pm_state = create_rpc_state(buf, count, CACHE_Tier_PM);
    hvac_prc_state* ssd_state = create_rpc_state(buf, count, CACHE_Tier_SSD);

    // 3. Determine the "remote fd" or server rank
    // map the local fd to the remote fd, which will be used by the RPC
    int remote_fd = fd_redir_map[fd];
    
    // Decide the rank of PM (since not all nodes have PM)
    int pm_rank = get_PM_rank();

    int ssd_rank = get_SSD_rank();

    // 4. Launch PM read RPC
    hvac_client_comm_gen_read_rpc(pm_rank, remote_fd, buf, count, offset,
                                    hvac_ms_read_cb, &state, CACHE_Tier_PM);

    // 5. Launch SSD read RPC
    hvac_client_comm_gen_read_rpc(ssd_rank, remote_fd, buf, count, offset,
                                    hvac_ms_read_cb, &state, CACHE_Tier_SSD);

    // 6. Wait for one request to succeed
    ssize_t final_result = -1;
    bool done = false;

    while(done)
    {
        pthread_mutex_lock(&pm_state.lock);
        bool pm_complete = pm_state->completed;
        ssize_t pm_result = pm_state->read_result;
        pthread_mutex_unlock(&pm_state.lock);

        pthread_mutex_lock(&ssd_state.lock);
        bool ssd_complete = ssd_state->completed;
        ssize_t ssd_result = ssd_state->read_result;
        pthread_mutex_unlock(&ssd_state.lock);

        if(pm_complete && pm_result >= 0)
        {
            final_result = pm_result;
            done = true;
        } else if (ssd_complete && ssd_result >= 0)
        {
            final_result = ssd_result;
            done = true;
        } else if (pm_complete && ssd_complete)             // both done but both failed
        {
            if(pm_result <0 && ssd_result < 0)
            {
                final_result = -1;
            } else 
            {
                final_result = std::max(pm_result, ssd_result);
            }
            done = true;
        }

        if(!done)
        {
            // TODO wait for a signal
        }
        
    }

    // 7. Clean up
    destroy_rpc_state(pm_state);
    destroy_rpc_state(ssd_state);

    // 8. Return whichever result completed first
    return final_result;
}