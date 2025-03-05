#include <pthread.h>
#include <cstdio>
#include <cstdlib>
#include <cassert>
#include <mutex>
#include <condition_variable>

#include "hvac_internal.h"        // For hvac_file_tracked, hvac_get_path, etc.
#include "hvac_comm.h"
#include "hvac_cache_policy.h"
#include "hvac_multi_source_read.h"
#include "hvac_logging.h"         // For L4C_INFO, L4C_ERR

extern std::map<int, int > fd_redir_map;

int get_PM_rank();
int get_SSD_rank();

static hg_return_t ms_read_cb(const struct hg_cb_info *info);

namespace hvac {
    ssize_t ms_read(int fd, void* buf, size_t count, off_t offset)
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
        ms_read_state *ms = (ms_read_state*) calloc(1, sizeof(ms_read_state));
        pthread_mutex_init(&ms->lock, NULL);
        pthread_cond_init(&ms->cond, NULL);
        ms->completed = false;
        ms->pm_done = false;
        ms->ssd_done = false;
        ms->pm_result = -1;
        ms->ssd_result= -1;

        hvac_rpc_state* pm_state = (hvac_rpc_state*) calloc(1, sizeof(hvac_rpc_state));
        pm_state->ms = ms;
        pm_state->buffer = buf;
        pm_state->size = count;
        pm_state->requested_tier = CACHE_TIER_FSDAX;


        hvac_rpc_state* ssd_state = (hvac_rpc_state*) calloc(1, sizeof(hvac_rpc_state));
        ssd_state->ms = ms;
        ssd_state->buffer = buf;
        ssd_state->size = count;
        ssd_state->requested_tier = CACHE_TIER_SSD;


        // 3. Determine the "remote fd" or server rank
        // map the local fd to the remote fd, which will be used by the RPC
        int remote_fd = fd_redir_map[fd];
        int pm_rank = get_PM_rank();
        int ssd_rank = get_SSD_rank();

        // 4. Launch PM read RPC
        hvac_client_comm_gen_read_rpc_with_ms(pm_rank, remote_fd, buf, count, offset,
                                        ms_read_cb, pm_state);

        // 5. Launch SSD read RPC
        hvac_client_comm_gen_read_rpc_with_ms(ssd_rank, remote_fd, buf, count, offset,
                                        ms_read_cb, ssd_state);

        // 6. Wait for one request to succeed
        ssize_t final_result = -1;
        bool done = false;

        pthread_mutex_lock(&ms->lock);

        while(!ms->completed) 
        {
            pthread_cond_wait(&ms->cond, &ms->lock);
        }

        if(ms->pm_done && ms->pm_result != -1) 
        {
            final_result = ms->pm_result;
            done = true;
        }
        else if(ms->ssd_done && ms->ssd_result != -1) 
        {
            final_result = ms->ssd_result;
            done = true;
        }
        pthread_mutex_unlock(&ms->lock);

        // 7. Clean up
        free(pm_state);
        free(ssd_state);
        pthread_mutex_destroy(&ms->lock);
        pthread_cond_destroy(&ms->cond);
        free(ms);
        // 8. Return whichever result completed first
        return final_result;
    }

} // namespace hvac


static hg_return_t ms_read_cb(const struct hg_cb_info *info)
{
    hvac_rpc_state* state = (hvac_rpc_state*) info->arg;
    ms_read_state* ms = state->ms;


    if(info->ret != HG_SUCCESS) 
    {
        L4C_INFO("RPC failed");
        pthread_mutex_lock(&ms->lock);
        if(state->requested_tier == CACHE_TIER_FSDAX) 
        {
            ms->pm_done = true;
            ms->pm_result = -1;
        }
        else if(state->requested_tier == CACHE_TIER_SSD) 
        {
            ms->ssd_done = true;
            ms->ssd_result = -1;
        }

        if (!ms->completed) {
            // check if the other request is done or not
            if ((ms->pm_done && ms->ssd_done) &&
                (ms->pm_result < 0 && ms->ssd_result < 0)) {
                // both fail => complete
                ms->completed = true;
                pthread_cond_signal(&ms->cond);
            }
        }
        pthread_mutex_unlock(&ms->lock);
        return HG_SUCCESS;
    }

    hvac_rpc_out_t out;
    hg_return_t ret = HG_Get_output(info->info.forward.handle, &out);
    if (ret != HG_SUCCESS) {
        // handle error
        pthread_mutex_lock(&ms->lock);
        if (state->requested_tier == CACHE_TIER_FSDAX) {
            ms->pm_done    = true;
            ms->pm_result  = -1;
        } else {
            ms->ssd_done   = true;
            ms->ssd_result = -1;
        }
        // check if both done => complete
        if (!ms->completed) {
            if ((ms->pm_done && ms->ssd_done) &&
                (ms->pm_result < 0 && ms->ssd_result < 0)) {
                ms->completed = true;
                pthread_cond_signal(&ms->cond);
            }
        }
        pthread_mutex_unlock(&ms->lock);
        return HG_SUCCESS;
    }

    ssize_t bytes_read = out.ret;
    hvac_rpc_state *rpc = state;

    ret = HG_Bulk_free(rpc->bulk_handle);
    ret = HG_Free_output(info->info.forward.handle, &out);
    ret = HG_Destroy(info->info.forward.handle);

    // success read, update ms
    pthread_mutex_lock(&ms->lock);
    if (rpc->requested_tier == CACHE_TIER_FSDAX) {
        ms->pm_done   = true;
        ms->pm_result = bytes_read;
    } else if (rpc->requested_tier == CACHE_TIER_SSD)
    {
        ms->ssd_done   = true;
        ms->ssd_result = bytes_read;
    }
    // if success => set ms->completed
    if (bytes_read >= 0 && !ms->completed) {
        ms->completed = true;
        pthread_cond_signal(&ms->cond);
    } else 
    {
        // if the other side also done or both fail => complete
        if ((ms->pm_done && ms->ssd_done) &&
            (ms->pm_result < 0 && ms->ssd_result < 0)) 
            {
            ms->completed = true;
            pthread_cond_signal(&ms->cond);
        }
    }
    pthread_mutex_unlock(&ms->lock);

    return HG_SUCCESS;
}


int get_PM_rank() 
{
    return 0;
    // TODO: Implement this function
}

int get_SSD_rank() 
{
    return 0;
    // TODO: Implement this function
}