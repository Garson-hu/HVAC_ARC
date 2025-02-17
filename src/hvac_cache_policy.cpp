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

/*
    Return the cache tier of the file
*/
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

/*
    Checks PM usage and evicts (moves) files to SSD if needed.
    Selects a victim file from PM based on some eviction policy 
    (for now, lowest access count) and returns its path. 
    This function only chooses a victim; it does not actually move the file.
    TODO: Maybe Highest access count?
    TODO: Better way to find the lowest access count 
    TODO: --> 1. std:: min_element --> not well suited, g_fileMetaMap is always changes I guess
    TODO: --> 2. std::priority_queue. Use a queue to maintain the smallest access count
    TODO: --> 3. std::multimap. Use a multimap to maintain the smallest access count

    * We don't only evict one file, but we can use a function call this one multi times to evict multiple files
*/
std::string cache_policy_select_victim_for_eviction()
{
    std::lock_guard<std::mutex> lock(g_cacheMutex);

    std::string victim_path = "";
    
    for(auto &file : g_fileMetaMap)
    {
        if(file.second.current_tier == CACHE_TIER_FSDAX && !file.second.is_open)                // Only consider the files in PM and closed
        {
            if(victim_path == "")
            {
                victim_path = file.first;
            } else if(file.second.access_count < g_fileMetaMap[victim_path].access_count)
            {
                victim_path = file.first;
            }
        }
    }

    return std::string(victim_path);
}

/*
    Removes a file from the metadata tracking. For example, if the file is no longer needed,
    or you want to clear the entry after eviction or unlinking the file.
    There is a similar funcion in HVAC called: hvac_remove_fd(int fd), but that one is for the file descriptor
*/
void cache_policy_remove_file(const std::string &path)
{
    std::lock_guard<std::mutex> lock(g_cacheMutex);

    if(g_fileMetaMap.find(path) == g_fileMetaMap.end())
    {
        L4C_WARN("hvac_cache_policy.cpp - cache_policy_remove_file - File not found in metadata: %s\n", path.c_str());
        exit(-1);
    }

    if(g_fileMetaMap.find(path)->second.current_tier == CACHE_TIER_FSDAX)
    {
        g_fsdax_used_bytes -= g_fileMetaMap.find(path)->second.size;
    } else if(g_fileMetaMap.find(path)->second.current_tier == CACHE_TIER_SSD)
    {
        g_ssd_used_bytes -= g_fileMetaMap.find(path)->second.size;
    }

    g_fileMetaMap.erase(path);
}

/*
    Evicts a file from the PM tier if needed to make space for new files.
    This function is called before adding a new file to the PM tier.
    The function checks the PM usage and evicts the (a few) least accessed file if the PM is full.
    The function returns the path of the victim file to evict, or an empty string if no eviction is needed.
*/
int cache_policy_evict_if_needed()
{
    std::lock_guard<std::mutex> lock(g_cacheMutex);
    
    if(g_fsdax_used_bytes <= g_fsdax_capacity_bytes)
    {
        return 0;                                                               // No eviction needed
    }

    // TODO: Maybe evict multiple files not just 1 file (I think it should be a percentage of the total files)
    std::string victim_path = cache_policy_select_victim_for_eviction();        // Select the victim file to evict

    // TODO: Maybe we can use a function to evict multiple files
    if(victim_path == "")
    {
        L4C_WARN("hvac_cache_policy.cpp - cache_policy_evict_if_needed - No victim file found for eviction\n");
        return -1;
    }

    // TODO: Maybe we can use a function to evict multiple files
    cache_policy_remove_file(victim_path);                                     // Remove the victim file from the metadata

    return 0;
}


void cache_policy_get_usage_bytes(uint64_t* used_pm_bytes, uint64_t* used_ssd_bytes)
{
    std::lock_guard<std::mutex> lock(g_cacheMutex);

    *used_pm_bytes = g_fsdax_used_bytes;
    *used_ssd_bytes = g_ssd_used_bytes;
}
