
#include <string>
#include <iostream>
#include <map>	

#include "hvac_comm.h"
// #include "hvac_cache_policy.h"
// #include "hvac_multi_source_read.h"
#include "hvac_data_mover_internal.h"

extern "C" {
#include "hvac_logging.h"
#include <fcntl.h>
#include <cassert>
#include <unistd.h>
}

/* RPC Block Constructs */
static hg_bool_t done = HG_FALSE;
static pthread_cond_t done_cond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t done_mutex = PTHREAD_MUTEX_INITIALIZER;


/* RPC Globals */
static hg_id_t hvac_client_rpc_id;
static hg_id_t hvac_client_open_id;
static hg_id_t hvac_client_close_id;
static hg_id_t hvac_client_seek_id;
ssize_t read_ret = -1;

/* Mercury Data Caching */
std::map<int, std::string> address_cache;  // Key: Rank, Value: Server Address
extern std::map<int, int > fd_redir_map;

extern std::map<int, std::string > fd_map;
extern "C" bool hvac_file_tracked(int fd);
extern "C" bool hvac_track_file(const char* path, int flags, int fd);

// Carry CB Information for CB
struct hvac_open_state{
    uint32_t            local_fd;
};

static hg_return_t
hvac_seek_cb(const struct hg_cb_info *info)
{
    hvac_seek_out_t out;
    ssize_t bytes_read = -1;

    HG_Get_output(info->info.forward.handle, &out);    
    //Set the SEEK OUTPUT
    bytes_read = out.ret;
    HG_Free_output(info->info.forward.handle, &out);
    HG_Destroy(info->info.forward.handle);

    /* signal to main() that we are done */
    pthread_mutex_lock(&done_mutex);
    done++;
    read_ret = bytes_read;
    pthread_cond_signal(&done_cond);
    pthread_mutex_unlock(&done_mutex);  
    return HG_SUCCESS;    
}

/*
struct hg_cb_info {
    union { 
        struct hg_cb_info_lookup lookup;
        struct hg_cb_info_forward forward;
        struct hg_cb_info_respond respond;
        struct hg_cb_info_bulk bulk;
    } info;
    void *arg;        
    hg_cb_type_t type; 
    hg_return_t ret;   
};
*/
static hg_return_t
hvac_open_cb(const struct hg_cb_info *info)
{
    // & struct include: int32_t ret_status
    hvac_open_out_t out;
    // & arg is void*, and it's the user data
    struct hvac_open_state *open_state = (struct hvac_open_state *)info->arg;    
    assert(info->ret == HG_SUCCESS);

    HG_Get_output(info->info.forward.handle, &out); 

    // & map the local fd to the remote fd, which will be used by the RPC
    fd_redir_map[open_state->local_fd] = out.ret_status;
    // L4C_INFO("Open RPC Returned FD %d\n",out.ret_status);
    HG_Free_output(info->info.forward.handle, &out);
    HG_Destroy(info->info.forward.handle);

    /* signal to main() that we are done */
    pthread_mutex_lock(&done_mutex);
    done++;
    pthread_cond_signal(&done_cond);
    pthread_mutex_unlock(&done_mutex);  
    return HG_SUCCESS;
}

/* callback triggered upon receipt of rpc response */
/* In this case there is no response since that call was response less */
static hg_return_t
hvac_read_cb(const struct hg_cb_info *info)
{
    hg_return_t ret;
    hvac_rpc_out_t out;
    ssize_t bytes_read = -1;
    struct hvac_rpc_state *hvac_rpc_state_p = (hvac_rpc_state *)info->arg;
    assert(info->ret == HG_SUCCESS);

    /* decode response */
    HG_Get_output(info->info.forward.handle, &out);
    bytes_read = out.ret;
    /* clean up resources consumed by this rpc */
    ret = HG_Bulk_free(hvac_rpc_state_p->bulk_handle);
	assert(ret == HG_SUCCESS);
	// L4C_INFO("INFO: Freeing Bulk Handle"); //Does this deregister memory?

	ret = HG_Free_output(info->info.forward.handle, &out);
	assert(ret == HG_SUCCESS);
    
	ret = HG_Destroy(info->info.forward.handle);
	assert(ret == HG_SUCCESS);
    
	free(hvac_rpc_state_p);

    /* signal to main() that we are done */
    pthread_mutex_lock(&done_mutex);
    done++;
    read_ret=bytes_read;
    pthread_cond_signal(&done_cond);
    pthread_mutex_unlock(&done_mutex);  
    return HG_SUCCESS;
}

void hvac_client_comm_register_rpc()
{   
    hvac_client_open_id = hvac_open_rpc_register();
    hvac_client_rpc_id = hvac_rpc_register();    
    hvac_client_close_id = hvac_close_rpc_register();
    hvac_client_seek_id = hvac_seek_rpc_register();
}

/*
    Blocks the current thread until an asynchronous remote procedure call (RPC) completes. 
    Specifically, it synchronizes operations by waiting for condition variables between threads.
*/
void hvac_client_block()
{
    /* wait for callbacks to finish */
    pthread_mutex_lock(&done_mutex);
    while (done != HG_TRUE)
        pthread_cond_wait(&done_cond, &done_mutex);
    pthread_mutex_unlock(&done_mutex);
}

ssize_t hvac_read_block()
{
    ssize_t bytes_read;
    /* wait for callbacks to finish */
    
    pthread_mutex_lock(&done_mutex);
    while (done != HG_TRUE)
        pthread_cond_wait(&done_cond, &done_mutex);
    bytes_read = read_ret;
    pthread_mutex_unlock(&done_mutex);
    return bytes_read;
}


ssize_t hvac_seek_block()
{
    size_t bytes_read;
    /* wait for callbacks to finish */
    pthread_mutex_lock(&done_mutex);
    while (done != HG_TRUE)
        pthread_cond_wait(&done_cond, &done_mutex);
    bytes_read = read_ret;
    pthread_mutex_unlock(&done_mutex);
    return bytes_read;
}


void hvac_client_comm_gen_close_rpc(uint32_t svr_hash, int fd)
{   
    hg_addr_t svr_addr; 
    hvac_close_in_t in;
    hg_handle_t handle; 
    int ret;
    /* Get address */
    svr_addr = hvac_client_comm_lookup_addr(svr_hash);        

    /* create create handle to represent this rpc operation */
    hvac_comm_create_handle(svr_addr, hvac_client_close_id, &handle);

    in.fd = fd_redir_map[fd];

    ret = HG_Forward(handle, NULL, NULL, &in);
    assert(ret == 0);

    fd_redir_map.erase(fd);

    HG_Destroy(handle);
    hvac_comm_free_addr(svr_addr);

    return;

}

/*
*    This function is used to open a file on the remote server
*    @param svr_hash: The hash of the server to connect to
*    @param path: The original path of the file to open
*    @param fd: The local file descriptor
*/
void hvac_client_comm_gen_open_rpc(uint32_t svr_hash, string path, int fd)
{
    hg_addr_t svr_addr;
    hvac_open_in_t in;
    hg_handle_t handle;
    struct hvac_open_state *hvac_open_state_p;  // & hvac_open_state is a struct which contains local_fd
    int ret;
    done = HG_FALSE;

    /* Get address according to server hash  */
    /* svr_hash is calculated as: ((fd_map[fd]) % g_hvac_server_count) */
    svr_addr = hvac_client_comm_lookup_addr(svr_hash);    

    /* Allocate args for callback pass through */
    hvac_open_state_p = (struct hvac_open_state *)malloc(sizeof(*hvac_open_state_p));
    hvac_open_state_p->local_fd = fd;

    /* create  handle to represent this rpc operation
        & warp the function from HG_Create()    
    */    
    hvac_comm_create_handle(svr_addr, hvac_client_open_id, &handle);  
    in.path = (hg_string_t)malloc(strlen(path.c_str()) + 1 );
    sprintf(in.path,"%s",path.c_str());
    
    ret = HG_Forward(handle, hvac_open_cb, hvac_open_state_p, &in);
    assert(ret == 0);
    /*
        & Warp the function from HG_Addr_free()
    */
    hvac_comm_free_addr(svr_addr);

    return;

}

void hvac_client_comm_gen_read_rpc(uint32_t svr_hash, int localfd, void *buffer, ssize_t count, off_t offset)
{
    hg_addr_t svr_addr;
    hvac_rpc_in_t in;
    const struct hg_info *hgi;
    int ret;
    struct hvac_rpc_state *hvac_rpc_state_p;
    done = HG_FALSE;
    read_ret = -1;

    /* Get address */
    svr_addr = hvac_client_comm_lookup_addr(svr_hash);

    /* set up state structure */
    hvac_rpc_state_p = (struct hvac_rpc_state *)malloc(sizeof(*hvac_rpc_state_p));
    hvac_rpc_state_p->size = count;


    /* This includes allocating a src buffer for bulk transfer */
    hvac_rpc_state_p->buffer = buffer;
    assert(hvac_rpc_state_p->buffer);
    //hvac_rpc_state_p->value = 5;

    /* create handle to represent this rpc operation */
    hvac_comm_create_handle(svr_addr, hvac_client_rpc_id, &(hvac_rpc_state_p->handle));

    /* register buffer for rdma/bulk access by server */
    hgi = HG_Get_info(hvac_rpc_state_p->handle);
    assert(hgi);
    ret = HG_Bulk_create(hgi->hg_class, 1, (void**) &(buffer),
       &(hvac_rpc_state_p->size), HG_BULK_WRITE_ONLY, &(in.bulk_handle));

    hvac_rpc_state_p->bulk_handle = in.bulk_handle;
    assert(ret == HG_SUCCESS);

    /* Send rpc. Note that we are also transmitting the bulk handle in the
     * input struct.  It was set above.
     */
    in.input_val = count;

    //Convert FD to remote FD
    in.accessfd = fd_redir_map[localfd];
	in.offset = offset;
    
    
    ret = HG_Forward(hvac_rpc_state_p->handle, hvac_read_cb, hvac_rpc_state_p, &in);
    assert(ret == 0);

    hvac_comm_free_addr(svr_addr);

    return;
}

void hvac_client_comm_gen_read_rpc_with_ms(uint32_t svr_hash, int localfd, void* buffer, ssize_t count, off_t offset,
                                            hg_cb_t callback, struct hvac_rpc_state* rpc_state)
{

    hg_addr_t svr_addr = hvac_client_comm_lookup_addr(svr_hash);
    hvac_rpc_in_t in;
    const struct hg_info *hgi;
    int ret;

    hvac_comm_create_handle(svr_addr, hvac_client_rpc_id, &(rpc_state->handle));
    
    /* register buffer for rdma/bulk access by server */
    rpc_state->size = count;
    rpc_state->buffer = buffer;
    assert(rpc_state->buffer);

    hgi = HG_Get_info(rpc_state->handle);
    assert(hgi);
    ret = HG_Bulk_create(hgi->hg_class, 1, (void**) &(buffer),
       &(rpc_state->size), HG_BULK_WRITE_ONLY, &(in.bulk_handle));
    assert(ret == HG_SUCCESS);

    rpc_state->bulk_handle = in.bulk_handle;

    in.input_val = count;
    in.accessfd = fd_redir_map[localfd];
    in.offset = offset;
    ret = HG_Forward(rpc_state->handle, callback, rpc_state, &in);
    assert(ret == 0);

    hvac_comm_free_addr(svr_addr);

    return;


}

void hvac_client_comm_gen_seek_rpc(uint32_t svr_hash, int fd, int offset, int whence)
{
    hg_addr_t svr_addr;
    hvac_seek_in_t in;
    hg_handle_t handle;
    read_ret = -1;
    int ret;
    done = HG_FALSE;

    /* Get address */
    svr_addr = hvac_client_comm_lookup_addr(svr_hash);    
    // L4C_INFO("DEBUG_HU: RUN Sequence: Client 6\n");

    /* Allocate args for callback pass through */    
    /* create create handle to represent this rpc operation */    
    hvac_comm_create_handle(svr_addr, hvac_client_seek_id, &handle);  

    in.fd = fd_redir_map[fd];
    in.offset = offset;
    in.whence = whence;
    

    ret = HG_Forward(handle, hvac_seek_cb, NULL, &in);
    assert(ret == 0);

    
    hvac_comm_free_addr(svr_addr);

    return;

}


//We've converted the filename to a rank
//Using standard c++ hashing modulo servers
//Find the address
hg_addr_t hvac_client_comm_lookup_addr(int rank)
{
	// L4C_INFO("Guangxing RANK %d", rank);
	if (address_cache.find(rank) != address_cache.end())
	{
        hg_addr_t target_server;
        HG_Addr_lookup2(hvac_comm_get_class(), address_cache[rank].c_str(), &target_server);
		return target_server;
	}

	/* The hardway */
	char filename[PATH_MAX];
	char svr_str[PATH_MAX];
	int svr_rank = -1;
	char *jobid = getenv("SLURM_JOBID");
	hg_addr_t target_server = nullptr;
	bool svr_found = false;
	FILE *na_config = NULL;
	sprintf(filename, "./.ports.cfg.%s", jobid);
	na_config = fopen(filename,"r+");
    

	while (fscanf(na_config, "%d %s\n",&svr_rank, svr_str) == 2)
	{
		if (svr_rank == rank){
			// L4C_INFO("Connecting to %s %d\n", svr_str, svr_rank);            
			svr_found = true;
            break;
		}
	}

	if (svr_found){
		//Do something
        address_cache[rank] = svr_str;
        HG_Addr_lookup2(hvac_comm_get_class(),svr_str,&target_server);		
	}

	return target_server;
}
