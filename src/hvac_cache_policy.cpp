#include "hvac_cache_policy.h"
#include <iostream>
#include <stdio.h>
#include "hvac_logging.h"


std::map<std::string, file_meta_t> g_fileMetaMap;       // Map of file paths to metadata

unit_64_t g_fsdax_capacity_bytes = 0;                   // Total capacity of PM in bytes        //TODO: Maybe a threshold for PM and SSD (80%?), not the total capacity
unit_64_t g_ssd_capacity_bytes = 0;                     // Total capacity of SSD in bytes
std::string g_fsdax_path;                               // Path or mount point for the PM tier
std::string g_ssd_path;                                 // Path or mount point for the SSD tier

unit_64_t g_fsdax_used_bytes = 0;                       // Used capacity of PM in bytes
unit_64_t g_ssd_used_bytes = 0;                         // Used capacity of SSD in bytes

std::mutex g_cacheMutex;                                // Mutex for thread safety

/**
 * Export all the required variables from sbatch script
 * 
 * @return 0 on success, non-zero otherwise
 */
int cache_policy_init() 
{
    if(getenv("HVAC_FSDAX_PATH") == NULL || getenv("HVAC_SSD_PATH") == NULL || getenv("HVAC_PM_CAPACITY") == NULL || getenv("HVAC_SSD_CAPACITY") == NULL) 
    {
        L4C_FATAL("Please set environment variables HVAC_FSDAX_PATH, HVAC_SSD_PATH, HVAC_FSDAX_CAPACITY, and HVAC_SSD_CAPACITY\n");
        exit(-1);
    }

    g_fsdax_path = getenv("HVAC_FSDAX_PATH");
    g_ssd_path = getenv("HVAC_SSD_PATH");
    g_fsdax_capacity_bytes = std::stoull(getenv("HVAC_FSDAX_CAPACITY"));
    g_ssd_capacity_bytes = std::stoull(getenv("HVAC_SSD_CAPACITY"));

    return 0;
}

/*
    Registers a file in the caching metadata, typically called during open or creation.
*/
void cache_policy_add_file(const std:string &path, uint64_t size_bytes)
{

    std::lock_guard<std::mutex> lock(g_cacheMutex);                         // Lock the mutex for thread safety since we are modifying the metadata map

    if(g_fileMetaMap.find(path) != map.end())
    {                                                                       // means that this file ready exists
        return;
    }

    
    file_meta_t newFileMeta;                                                // Create a new file metadata object            
    newFileMeta.path = path;
    newFileMeta.size = size_bytes;
    newFileMeta.access_count = 0;
    newFileMeta.is_open = true;                                            // Initially open // TODO : check if this is correct

    
    if(g_fsdax_used_bytes + size_bytes <= g_fsdax_capacity_bytes)          // Assign the file to the PM tier if there is enough space
    {
        newFileMeta.current_tier = CACHE_TIER_FSDAX;
        g_fsdax_used_bytes += size_bytes;
    } else if
    (
        g_ssd_used_bytes + size_bytes <= g_ssd_capacity_bytes){
        newFileMeta.current_tier = CACHE_TIER_SSD;
        g_ssd_used_bytes += size_bytes;
    } else 
    {
        newFileMeta.current_tier = CACHE_TIER_PFS;
        return;                                                            // No space available    
    }

    
    g_fileMetaMap[path] = newFileMeta;                                     // Add the file to the metadata map
    
}



void cache_policy_update_access(const std::string &path)
{
    std::lock_guard<std::mutex> lock(g_cacheMutex);

    if (g_fileMetaMap.find(path) == g_fileMetaMap.end())
    {
        return;                                                             // File not found
    }else
    {
        g_fileMetaMap[path].access_count++;                                 // Increment the access count
    }
}

/*
    TODO: There is an issue about update_tier function:
    TODO: 1. Should I only implement the function to move data from PM to SSD?
    TODO: 2. Should I implement the function to move data from SSD to PM? 
    TODO:    --> Like prefetch some data from SSD to PM, since the data already used will not be used again in the same epoch
    
*/
void cache_policy_update_tier(const std::string &path, cache_tier_t new_tier)
{
    std::lock_guard<std::mutex> lock(g_cacheMutex);

    if(g_fileMetaMap.find(path) == g_fileMetaMap.end())
    {
        L4C_WARN("hvac_cache_policy.cpp - cache_policy_update_tier - File not found in metadata: %s\n", path.c_str());
        return;
    }
    if(g_fileMetaMap[path].current_tier == new_tier)
    {
        L4C_DEBUG("hvac_cache_policy.cpp - cache_policy_update_tier - File already in the desired tier: %s\n", path.c_str());
        return;                                                            
    }

    if(new_tier == CACHE_TIER_SSD)                                        // Evict from PM to SSD
    {
        if(g_fileMetaMap[path].current_tier == CACHE_TIER_FSDAX)
        {
            if(g_ssd_used_bytes + g_fileMetaMap[path].size <= g_ssd_capacity_bytes)
            {
                g_ssd_used_bytes += g_fileMetaMap[path].size;
                g_fsdax_used_bytes -= g_fileMetaMap[path].size;
                g_fileMetaMap[path].current_tier = CACHE_TIER_SSD;
            }
            else
            {
                L4C_WARN("hvac_cache_policy.cpp - cache_policy_update_tier - Not enough space in SSD to move file: %s\n", path.c_str());
                exit(-1);
            }
        }
    }

    if(new_tier == CACHE_TIER_FSDAX)                                        // Prefetch from SSD to PM // TODO: Maybe not need now
    {
        if(g_fileMetaMap[path].current_tier == CACHE_TIER_SSD)
        {
            if(g_fsdax_used_bytes + g_fileMetaMap[path].size <= g_fsdax_capacity_bytes)
            {
                g_fsdax_used_bytes += g_fileMetaMap[path].size;
                g_ssd_used_bytes -= g_fileMetaMap[path].size;
                g_fileMetaMap[path].current_tier = CACHE_TIER_FSDAX;
            }
            else
            {
                L4C_WARN("hvac_cache_policy.cpp - cache_policy_update_tier - Not enough space in PM to move file: %s\n", path.c_str());
            }
        }
    }

}

/*
    Marks a file as open or closed in the metadata.
    When select victim, only consider the files that are closed
*/
void cache_policy_set_open_state(const std::string &path, bool is_open)
{
    std::lock_guard<std::mutex> lock(g_cacheMutex);

    if(g_fileMetaMap.find(path) == g_fileMetaMap.end())
    {
        L4C_FATAL("hvac_cache_policy.cpp - cache_policy_set_open_state - File not found in metadata: %s\n", path.c_str());          // Every files should have their own metadata
        exit(-1);
    }

    if(is_open)                                                                 // If we want to set the file as open
    {
        g_fileMetaMap[path].is_open = true;
    } else
    {
        g_fileMetaMap[path].is_open = false;
    }   

}

cache_tier_t cache_policy_get_tier(const std::string &path)
{
    std::lock_guard<std::mutex> lock(g_cacheMutex);

    if(g_fileMetaMap.find(path) == g_fileMetaMap.end())
    {
        L4C_WARN("hvac_cache_policy.cpp - cache_policy_get_tier - File not found in metadata: %s\n", path.c_str());
        return CACHE_TIER_UNKNOWN;
    }

    return g_fileMetaMap[path].current_tier;
}