#ifndef __HVAC_RPC_ENGINE_INTERNAL_H__
#define __HVAC_RPC_ENGINE_INTERNAL_H__

extern "C" {
#include <mercury.h>
#include <mercury_bulk.h>
#include <mercury_macros.h>
#include <mercury_proc_string.h>
}

// #include "hvac_multi_source_read.h"
// #include "hvac_cache_policy.h"
#include <string>
using namespace std;

struct ms_read_state;

/* visible API for example RPC operation */

//RPC Open Handler
MERCURY_GEN_PROC(hvac_open_out_t, ((int32_t)(ret_status)))
/*
typedef struct {
    int32_t ret_status;
} hvac_open_out_t;
*/

MERCURY_GEN_PROC(hvac_open_in_t, ((hg_string_t)(path)))
/*
typedef struct {
    hg_string_t path;
} hvac_open_in_t;
*/


//BULK Read Handler
MERCURY_GEN_PROC(hvac_rpc_out_t, ((int32_t)(ret)))
MERCURY_GEN_PROC(hvac_rpc_in_t, ((int32_t)(input_val))((hg_bulk_t)(bulk_handle))((int32_t)(accessfd))((int64_t)(offset)))

//RPC Seek Handler
MERCURY_GEN_PROC(hvac_seek_out_t, ((int32_t)(ret)))
/*
    typedef struct {
        int32_t ret;
    } hvac_seek_out_t;
*/
MERCURY_GEN_PROC(hvac_seek_in_t, ((int32_t)(fd))((int32_t)(offset))((int32_t)(whence)))


//Close Handler input arg
MERCURY_GEN_PROC(hvac_close_in_t, ((int32_t)(fd)))


//General
void hvac_init_comm(hg_bool_t listen);
void *hvac_progress_fn(void *args);
void hvac_comm_list_addr();
void hvac_comm_create_handle(hg_addr_t addr, hg_id_t id, hg_handle_t *handle);
void hvac_shutdown_comm();
void hvac_comm_free_addr(hg_addr_t addr);

//Retrieve the static variables
hg_class_t *hvac_comm_get_class();
hg_context_t *hvac_comm_get_context();


//Client
void hvac_client_comm_gen_seek_rpc(uint32_t svr_hash, int fd, int offset, int whence);
void hvac_client_comm_gen_read_rpc(uint32_t svr_hash, int localfd, void* buffer, ssize_t count, off_t offset);

// Multi source read function
void hvac_client_comm_gen_read_rpc_with_ms(uint32_t svr_hash, int localfd, void* buffer, ssize_t count, off_t offset,
                                            hg_cb_t callback, struct hvac_rpc_state* rpc_state);
void hvac_client_comm_gen_open_rpc(uint32_t svr_hash, string path, int fd);
void hvac_client_comm_gen_close_rpc(uint32_t svr_hash, int fd);
hg_addr_t hvac_client_comm_lookup_addr(int rank);
void hvac_client_comm_register_rpc();
void hvac_client_block();
ssize_t hvac_read_block();
ssize_t hvac_seek_block();



//Mercury common RPC registration
hg_id_t hvac_rpc_register(void);
hg_id_t hvac_open_rpc_register(void);
hg_id_t hvac_close_rpc_register(void);
hg_id_t hvac_seek_rpc_register(void);

/* struct used to carry state of overall operation across callbacks */
struct hvac_rpc_state {
    uint32_t            value;
    hg_size_t           size;
    void                *buffer;
    hg_bulk_t           bulk_handle;
    hg_handle_t         handle;
    hvac_rpc_in_t       in;


    // A pointer back to the ms_read_state, so the callback can update high-level info
    // ms_read_state* ms;   
    // cache_tier_t        requested_tier;  
};
#endif

