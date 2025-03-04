/* Data mover responsible for maintaining the NVMe state
 * and prefetching the data
 */
#include <filesystem>
#include <string>
#include <queue>
#include <iostream>

#include <pthread.h>
#include <string.h>
#include <chrono>
#include "hvac_logging.h"
#include "hvac_data_mover_internal.h"
using namespace std;
namespace fs = std::filesystem;

pthread_cond_t data_cond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t data_mutex = PTHREAD_MUTEX_INITIALIZER;

map<int,string> fd_to_path;             // & Server File Descriptor -> Original path
map<string, string> path_cache_map;     // & Original path -> Redirection path
queue<string> data_queue;               // & List of files to be moved


void *hvac_data_mover_fn(void *args)
{
    queue<string> local_list;

    if (getenv("BBPATH") == NULL){
        L4C_ERR("Set BBPATH Prior to using HVAC");        
    }

    string nvmepath = string(getenv("BBPATH")) + "/XXXXXX";    

    while (1) {
        pthread_mutex_lock(&data_mutex);
        pthread_cond_wait(&data_cond, &data_mutex);
        
        /* We can do stuff here when signaled */
        while (!data_queue.empty()){
            local_list.push(data_queue.front());
            data_queue.pop();
        }

        pthread_mutex_unlock(&data_mutex);

        /* Now we copy the local list to the NVMes*/
        while (!local_list.empty())
        {
            char *newdir = (char *)malloc(strlen(nvmepath.c_str())+1);
            strcpy(newdir, nvmepath.c_str());
            char *dir_name = mkdtemp(newdir); // & Create the directory "/XXXXXX" in nvmepath

            if(dir_name == NULL)
                fprintf(stderr, "%s dir creation failed\n", newdir);
            
            string dirpath = newdir;
            string filename = dirpath + string("/") + fs::path(local_list.front().c_str()).filename().string();
            try{
                
                // if (DEBUG_HU)  L4C_INFO("DEBUG_HU: Copying file from local to nvmes Start");
                // auto start = std::chrono::high_resolution_clock::now();
                // L4C_INFO("DEBUG_HU: Copying file from local to nvmes Start");
                fs::copy(local_list.front(), filename);
                // auto end = std::chrono::high_resolution_clock::now();
                // if (DEBUG_HU)  L4C_INFO("DEBUG_HU: Copying file from local to nvmes End");
                // std::chrono::duration<double> elapsed = end - start;
                // L4C_INFO("DEBUG_HU: Elapsed time %f seconds\n", elapsed.count());
                // printf("DEBUG_HU: Elapsed time %f seconds\n", elapsed.count());

                path_cache_map[local_list.front()] = filename;

            } catch (const fs::filesystem_error& e)
            {
                fprintf(stderr, "Error : %s copying from %s to %s\n", e.what(), e.path1(), e.path2());
                L4C_INFO("Failed to copy %s to %s\n",local_list.front().c_str(), filename.c_str());
            }        
            
            local_list.pop();
        }
    }
    return NULL;
}
