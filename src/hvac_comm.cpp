/*
    hvac_comm.cpp is mainly responsible for server-side communication initialization 
    and RPC processing. 
    It uses the Mercury library to realize high-performance RPC communication.
*/

#include "hvac_comm.h"
#include "hvac_data_mover_internal.h"

extern "C" {
#include "hvac_logging.h"
#include <fcntl.h>
#include <cassert>
//#include <pmi.h>
#include <unistd.h>
}


#include <string>
#include <iostream>
#include <map>	


static hg_class_t *hg_class = NULL;
static hg_context_t *hg_context = NULL;
static int hvac_progress_thread_shutdown_flags = 0;
static int hvac_server_rank = -1;
static int server_rank = -1;

/* struct used to carry state of overall operation across callbacks */
struct hvac_rpc_state {
    hg_size_t size;
    void *buffer;
    hg_bulk_t bulk_handle;
    hg_handle_t handle;
    hvac_rpc_in_t in;
};

//Initialize communication for both the client and server
//processes
//This is based on the rpc_engine template provided by the mercury lib
void hvac_init_comm(hg_bool_t listen)
{

	const char *info_string = "ofi+verbs;ofi_rxm://";  
    /* 	int *pid_server = NULL;
        PMI_Get_rank(pid_server);
        L4C_INFO("PMI Rank ID: %d \n", pid_server);  
            
        std::string rankstr_str = std::to_string(*pid_server);

            // Set the environment variable
            if (setenv("PMIX_RANK", rankstr_str.c_str(), 1) != 0) 
            {
            L4C_INFO("Exported PMIX_RANK: %s \n", rankstr_str.c_str());
            }
    */
    char *rank_str = getenv("SLURM_PROCID"); // Get the rank of the server 
	server_rank = atoi(rank_str);
    //	L4C_INFO("PMIX_RANK: %s Server Rank: %d \n", rankstr_str.c_str(), server_rank);
	L4C_INFO("Server Rank: %d \n", server_rank);

	pthread_t hvac_progress_tid;

    HG_Set_log_level("DEBUG");

    /* Initialize Mercury with the desired network abstraction class */
    hg_class = HG_Init(info_string, listen);
	if (hg_class == NULL){
		L4C_FATAL("Failed to initialize HG_CLASS Listen Mode : %d : PMI_RANK %d \n", listen, server_rank);
	}

    /* Create HG context */
    hg_context = HG_Context_create(hg_class);
	if (hg_context == NULL){
		L4C_FATAL("Failed to initialize HG_CONTEXT\n");
	}
	//Only for server processes
	if (listen)
	{
		if (rank_str != NULL){
			hvac_server_rank = atoi(rank_str);
		}else
		{
			L4C_FATAL("Failed to extract rank\n");
		}
	}

	//  L4C_INFO("Mecury initialized");
    //  free(rank_str);
	//  free(pid_server);
	//TODO The engine creates a pthread here to do the listening and progress work
	// ! I need to understand this better I don't want to create unecessary work for the client
    // ! For now, just create the progress thread
	if (pthread_create(&hvac_progress_tid, NULL, hvac_progress_fn, NULL) != 0){
		L4C_FATAL("Failed to initialized mecury progress thread\n");
	}

}

/**
 * @brief Shuts down the HVAC communication.
 *
 * This function sets the shutdown flag for the progress thread and, if the 
 * Mercury context is initialized, proceeds with shutting down the context 
 * and finalizing the Mercury class.
 */
void hvac_shutdown_comm()
{
    hvac_progress_thread_shutdown_flags = true;

    if (hg_context == NULL)
	return;
 
/*
    int ret = -1;
    ret = HG_Context_destroy(hg_context);
    assert(ret == HG_SUCCESS);

    ret = HG_Finalize(hg_class);
    assert(ret == HG_SUCCESS);
*/

}


void *hvac_progress_fn(void *args)
{
	hg_return_t ret;
	unsigned int actual_count = 0;
    // hvac_progress_thread_shutdown_flags in initialized as 0, so always true if not invoke hvac_shutdown_comm()
	while (!hvac_progress_thread_shutdown_flags){
		do{
			ret = HG_Trigger(hg_context, 0, 1, &actual_count);
		} while (
			(ret == HG_SUCCESS) && actual_count && !hvac_progress_thread_shutdown_flags);
		if (!hvac_progress_thread_shutdown_flags)
			HG_Progress(hg_context, 100);
	}
	
	return NULL;
}

/* I think only servers need to post their addresses. 
   There is an expectation that the server will be started in 
   advance of the clients. 
   Should the servers be started with an argument regarding the number of servers? 
   ! If I need to create two servers, I need to post all of them's addresses in here I think for now
   */
void hvac_comm_list_addr()
{
	char self_addr_string[PATH_MAX];
	char filename[PATH_MAX];
        hg_addr_t self_addr;
	FILE *na_config = NULL;
	hg_size_t self_addr_string_size = PATH_MAX;
    //	char *stepid = getenv("PMIX_NAMESPACE");

	char *jobid =  getenv("SLURM_JOBID");
    L4C_INFO("JOB_ID: %s\n", jobid); 

	sprintf(filename, "./.ports.cfg.%s", jobid);
	/* Get self addr to tell client about */
    HG_Addr_self(hg_class, &self_addr);
    HG_Addr_to_string(hg_class, self_addr_string, &self_addr_string_size, self_addr);
    HG_Addr_free(hg_class, self_addr);
    

    /* Write addr to a file */
    na_config = fopen(filename, "a+");
    if (!na_config) {
        L4C_ERR("Could not open config file from: %s\n", filename);
        exit(0);
    }
    fprintf(na_config, "%d %s\n", hvac_server_rank, self_addr_string);
    fclose(na_config);
}



/* callback triggered upon completion of bulk transfer */
static hg_return_t
hvac_rpc_handler_bulk_cb(const struct hg_cb_info *info)
{
    struct hvac_rpc_state *hvac_rpc_state_p = (struct hvac_rpc_state*)info->arg;
    int ret;
    hvac_rpc_out_t out;
    out.ret = hvac_rpc_state_p->size;

    assert(info->ret == 0);

    ret = HG_Respond(hvac_rpc_state_p->handle, NULL, NULL, &out);
    assert(ret == HG_SUCCESS);        

    HG_Bulk_free(hvac_rpc_state_p->bulk_handle);
    L4C_INFO("Info Server: Freeing Bulk Handle\n");
    HG_Destroy(hvac_rpc_state_p->handle);
    free(hvac_rpc_state_p->buffer);
    free(hvac_rpc_state_p);
    return (hg_return_t)0;
}



static hg_return_t
hvac_rpc_handler(hg_handle_t handle)
{
    int ret;
    struct hvac_rpc_state *hvac_rpc_state_p;
    const struct hg_info *hgi;
    ssize_t readbytes;

    hvac_rpc_state_p = (struct hvac_rpc_state*)malloc(sizeof(*hvac_rpc_state_p));

    /* decode input */
    HG_Get_input(handle, &hvac_rpc_state_p->in);   
    
    /* This includes allocating a target buffer for bulk transfer */
    hvac_rpc_state_p->buffer = calloc(1, hvac_rpc_state_p->in.input_val);
    assert(hvac_rpc_state_p->buffer);

    hvac_rpc_state_p->size = hvac_rpc_state_p->in.input_val;
    hvac_rpc_state_p->handle = handle;



    /* register local target buffer for bulk access */

    hgi = HG_Get_info(handle);
    assert(hgi);
    ret = HG_Bulk_create(hgi->hg_class, 1, &hvac_rpc_state_p->buffer,
        &hvac_rpc_state_p->size, HG_BULK_READ_ONLY,
        &hvac_rpc_state_p->bulk_handle);
    assert(ret == 0);

    if (hvac_rpc_state_p->in.offset == -1){
        readbytes = read(hvac_rpc_state_p->in.accessfd, hvac_rpc_state_p->buffer, hvac_rpc_state_p->size);
        L4C_DEBUG("Server Rank %d : Read %ld bytes from file %s", server_rank,readbytes, fd_to_path[hvac_rpc_state_p->in.accessfd].c_str());
    }else
    {
        readbytes = pread(hvac_rpc_state_p->in.accessfd, hvac_rpc_state_p->buffer, hvac_rpc_state_p->size, hvac_rpc_state_p->in.offset);
        L4C_DEBUG("Server Rank %d : PRead %ld bytes from file %s at offset %ld", server_rank,readbytes, fd_to_path[hvac_rpc_state_p->in.accessfd].c_str(),hvac_rpc_state_p->in.offset );
    }

    //Reduce size of transfer to what was actually read 
    //We may need to revisit this.
    hvac_rpc_state_p->size = readbytes;

    /* initiate bulk transfer from client to server */
    ret = HG_Bulk_transfer(hgi->context, hvac_rpc_handler_bulk_cb, hvac_rpc_state_p,
        HG_BULK_PUSH, hgi->addr, hvac_rpc_state_p->in.bulk_handle, 0,
        hvac_rpc_state_p->bulk_handle, 0, hvac_rpc_state_p->size, HG_OP_ID_IGNORE);
    
    assert(ret == 0);
    (void) ret;

    return (hg_return_t)ret;
}





/**
 * @brief Handle an open RPC request from a client.
 *
 * This function is invoked on the server when a client makes an RPC request
 * to open a file.  It is responsible for opening the file and returning the
 * file descriptor to the client.
 *
 * @param[in] handle The handle associated with this RPC request.
 *
 * @return An HG return code indicating the status of the RPC.
 */
static hg_return_t
hvac_open_rpc_handler(hg_handle_t handle)
{
    hvac_open_in_t in;
    hvac_open_out_t out;    
    int ret = HG_Get_input(handle, &in);
    assert(ret == 0);
    string redir_path = in.path;
    // path_cache_map in hvac_data_mover_internal.h 
    // extern map<string, string> path_cache_map;
    if (path_cache_map.find(redir_path) != path_cache_map.end())
    {
        L4C_INFO("Server Rank %d : Successful Redirection %s to %s", server_rank, redir_path.c_str(), path_cache_map[redir_path].c_str());
        redir_path = path_cache_map[redir_path];
    }
    L4C_INFO("Server Rank %d : Successful Open %s", server_rank, in.path);    
    out.ret_status = open(redir_path.c_str(),O_RDONLY);  
    fd_to_path[out.ret_status] = in.path;  
    HG_Respond(handle,NULL,NULL,&out);

    return (hg_return_t)ret;

}

static hg_return_t
hvac_close_rpc_handler(hg_handle_t handle)
{
    hvac_close_in_t in;
    int ret = HG_Get_input(handle, &in);
    assert(ret == HG_SUCCESS);

    L4C_INFO("Closing File %d\n",in.fd);
    ret = close(in.fd);
    assert(ret == 0);

    //Signal to the data mover to copy the file
    if (path_cache_map.find(fd_to_path[in.fd]) == path_cache_map.end())
    {
        L4C_INFO("Caching %s",fd_to_path[in.fd].c_str());
        pthread_mutex_lock(&data_mutex);
        pthread_cond_signal(&data_cond);
        data_queue.push(fd_to_path[in.fd]);
        pthread_mutex_unlock(&data_mutex);
    }

	fd_to_path.erase(in.fd);
    return (hg_return_t)ret;
}

static hg_return_t
hvac_seek_rpc_handler(hg_handle_t handle)
{
    hvac_seek_in_t in;
    hvac_seek_out_t out;    
    int ret = HG_Get_input(handle, &in);
    assert(ret == 0);

    out.ret = lseek64(in.fd, in.offset, in.whence);

    HG_Respond(handle,NULL,NULL,&out);

    return (hg_return_t)ret;
}


/* register this particular rpc type with Mercury */
hg_id_t
hvac_rpc_register(void)
{
    hg_id_t tmp;
    // & replace HG_Register()
    tmp = MERCURY_REGISTER(
        hg_class, "hvac_base_rpc", hvac_rpc_in_t, hvac_rpc_out_t, hvac_rpc_handler);

    return tmp;
}

hg_id_t
hvac_open_rpc_register(void)
{
    hg_id_t tmp;

    tmp = MERCURY_REGISTER(
        hg_class, "hvac_open_rpc", hvac_open_in_t, hvac_open_out_t, hvac_open_rpc_handler);

    return tmp;
}

hg_id_t
hvac_close_rpc_register(void)
{
    hg_id_t tmp;

    tmp = MERCURY_REGISTER(
        hg_class, "hvac_close_rpc", hvac_close_in_t, void, hvac_close_rpc_handler);
    

    int ret =  HG_Registered_disable_response(hg_class, tmp,
                                           HG_TRUE);                        
    assert(ret == HG_SUCCESS);

    return tmp;
}

/* register this particular rpc type with Mercury */
hg_id_t
hvac_seek_rpc_register(void)
{
    hg_id_t tmp;

    tmp = MERCURY_REGISTER(
        hg_class, "hvac_seek_rpc", hvac_seek_in_t, hvac_seek_out_t, hvac_seek_rpc_handler);

    return tmp;
}

/* Create context even for client */
void
hvac_comm_create_handle(hg_addr_t addr, hg_id_t id, hg_handle_t *handle)
{    
    hg_return_t ret = HG_Create(hg_context, addr, id, handle);

    assert(ret==HG_SUCCESS);    
}

/*Free the addr */
void 
hvac_comm_free_addr(hg_addr_t addr)
{
    hg_return_t ret = HG_Addr_free(hg_class,addr);
    assert(ret==HG_SUCCESS);
}

hg_class_t *hvac_comm_get_class()
{
    return hg_class;
}

hg_context_t *hvac_comm_get_context()
{
    return hg_context;
}
