#include <pthread.h>
#include <cstdio>
#include <cstdlib>
#include <cassert>
#include <mutex>
#include <condition_variable>

#include "hvac_comm.h"
#include "hvac_cache_policy.h"
#include "hvac_multi_source_read.h"
#include "hvac_internal.h"        // For hvac_file_tracked, hvac_get_path, etc.
#include "hvac_logging.h"         // For L4C_INFO, L4C_ERR

int get_PM_rank();
int get_SSD_rank();

static hg_return_t ms_read_cb(const struct hg_cb_info *info);

namespace hvac {
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
        ms_read_state *ms = new ms_read_state;
        ms->completed = false;
        ms->pm_done = false;
        ms->ssd_done = false;

        hvac_rpc_state* pm_state = create_rpc_state(buf, count, CACHE_Tier_PM);
        pm_state->ms = ms;
        pm_state->requested_tier = CACHE_Tier_PM;

        hvac_rpc_state* ssd_state = create_rpc_state(buf, count, CACHE_Tier_SSD);
        ssd_state->ms = ms;
        ssd_state->requested_tier = CACHE_Tier_SSD;

        // 3. Determine the "remote fd" or server rank
        // map the local fd to the remote fd, which will be used by the RPC
        int remote_fd = fd_redir_map[fd];
        
        // Decide the rank of PM (since not all nodes have PM)
        int pm_rank = get_PM_rank();

        int ssd_rank = get_SSD_rank();

        // 4. Launch PM read RPC
        hvac_client_comm_gen_read_rpc_ms(pm_rank, remote_fd, buf, count, offset,
                                        ms_read_cb, pm_state, CACHE_Tier_PM);

        // 5. Launch SSD read RPC
        hvac_client_comm_gen_read_rpc_ms(ssd_rank, remote_fd, buf, count, offset,
                                        ms_read_cb, ssd_state, CACHE_Tier_SSD);

        // 6. Wait for one request to succeed
        ssize_t final_result = -1;
        bool done = false;

        while(!done)
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
                // TODO wait for a signal or Global condition variable
            }
            
        }

        // 7. Clean up
        destroy_rpc_state(pm_state);
        destroy_rpc_state(ssd_state);

        // 8. Return whichever result completed first
        return final_result;
    }

} // namespace hvac


static hg_return_t ms_read_cb(const struct hg_cb_info *info)
{
    hvac_rpc_state* state = (hvac_rpc_state*) info->arg;

    hg_return_t ret;
    hg_rpc_out_t out;

    // parse the output
    ret = HG_Get_output(info->info.forward.handle, &out);
    assert(info->ret == HG_SUCCESS);

    if(ret != HG_SUCCESS)
    {
        L4C_ERR("Failed to get output from RPC");
        pthread_mutex_lock(&state->lock);
        state->read_result = -1;
        state->completed   = true;
        pthread_mutex_unlock(&st->lock);

        // ? should we signal the main thread?
        return HG_SUCCESS;
    }

    ssize_t bytes_read = out.ret;                                   // number of bytes read by the RPC 

    // free the output
    ret = HG_Bulk_free(state->bulk_handle);
    ret = HG_Free_output(info->info.forward.handle, &out);
    ret = HG_Destroy(info->info.forward.handle);

    // fill result
    pthread_mutex_lock(&state->lock);
    state->read_result = bytes_read;
    state->completed = true;
    pthread_mutex_unlock(&state->lock);

    return HG_SUCCESS;
}


int get_PM_rank() 
{
    // TODO: Implement this function
}

int get_SSD_rank() 
{
    // TODO: Implement this function
}