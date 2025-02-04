#include "hvac_cache_policy.h"
#include <map>
#include <string>
#include <cstdint>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>

// Use the standard namespace for convenience
using namespace std;

// Global variables to track file metadata and capacity usage
static std::map<std::string, file_meta_t> g_fileMetaMap;

// Global capacity and usage variables
static uint64_t g_pmCapacityBytes = 0;
static uint64_t g_ssdCapacityBytes = 0;
static uint64_t g_pmUsedBytes = 0;
static uint64_t g_ssdUsedBytes = 0;

// Global base paths for PM and SSD tiers
static std::string g_pmBasePath;
static std::string g_ssdBasePath;

// Mutex to protect global变量
static pthread_mutex_t g_cacheMutex = PTHREAD_MUTEX_INITIALIZER;

/**
 * Initialize the cache policy module.
 *
 * @param pm_path           Path or mount point for the PM tier.
 * @param ssd_path          Path or mount point for the SSD tier.
 * @param pm_capacity_bytes Total capacity of PM in bytes.
 * @param ssd_capacity_bytes Total capacity of SSD in bytes.
 *
 * @return 0 on success, non-zero on failure.
 */
int cache_policy_init(const char* pm_path,
                      const char* ssd_path,
                      uint64_t pm_capacity_bytes,
                      uint64_t ssd_capacity_bytes)
{
    pthread_mutex_lock(&g_cacheMutex);
    g_pmBasePath = (pm_path != NULL) ? std::string(pm_path) : "";
    g_ssdBasePath = (ssd_path != NULL) ? std::string(ssd_path) : "";
    g_pmCapacityBytes = pm_capacity_bytes;
    g_ssdCapacityBytes = ssd_capacity_bytes;
    g_pmUsedBytes = 0;
    g_ssdUsedBytes = 0;
    g_fileMetaMap.clear();
    pthread_mutex_unlock(&g_cacheMutex);

    printf("[CachePolicy] Initialized: PM cap = %lu, SSD cap = %lu\n",
           (unsigned long)pm_capacity_bytes, (unsigned long)ssd_capacity_bytes);
    return 0;
}

/**
 * Finalize the cache policy module, releasing resources.
 */
void cache_policy_finalize()
{
    pthread_mutex_lock(&g_cacheMutex);
    g_fileMetaMap.clear();
    g_pmBasePath.clear();
    g_ssdBasePath.clear();
    g_pmCapacityBytes = 0;
    g_ssdCapacityBytes = 0;
    g_pmUsedBytes = 0;
    g_ssdUsedBytes = 0;
    pthread_mutex_unlock(&g_cacheMutex);
}

/**
 * Registers a file in the cache policy metadata.
 * Typically called during file open or creation.
 *
 * @param path        The original file path.
 * @param size_bytes  File size in bytes.
 */
void cache_policy_add_file(const char* path, uint64_t size_bytes)
{
    if (path == NULL) return;
    pthread_mutex_lock(&g_cacheMutex);
    std::string key(path);
    
    // If the file already exists, update its size.
    if (g_fileMetaMap.find(key) != g_fileMetaMap.end()) {
        g_fileMetaMap[key].size = size_bytes;
        pthread_mutex_unlock(&g_cacheMutex);
        return;
    }
    
    // Create a new metadata entry
    file_meta_t meta;
    meta.path = key;
    meta.size = size_bytes;
    meta.access_count = 0;
    meta.is_open = true; // File is open when first added
    
    // Try to place the file in PM first.
    if (g_pmUsedBytes + size_bytes <= g_pmCapacityBytes) {
        meta.current_tier = CACHE_TIER_PM;
        g_pmUsedBytes += size_bytes;
    } else {
        // If PM is full, try SSD; otherwise, fallback to PFS.
        if (g_ssdUsedBytes + size_bytes <= g_ssdCapacityBytes) {
            meta.current_tier = CACHE_TIER_SSD;
            g_ssdUsedBytes += size_bytes;
        } else {
            meta.current_tier = CACHE_TIER_PFS;
        }
    }
    
    // Add the metadata entry into the map.
    g_fileMetaMap[key] = meta;
    pthread_mutex_unlock(&g_cacheMutex);
}

/**
 * Updates the access count for the specified file.
 *
 * @param path The original file path.
 */
void cache_policy_update_access(const char* path)
{
    if (path == NULL) return;
    pthread_mutex_lock(&g_cacheMutex);
    std::string key(path);
    auto it = g_fileMetaMap.find(key);
    if (it != g_fileMetaMap.end()) {
        it->second.access_count++;
    }
    pthread_mutex_unlock(&g_cacheMutex);
}

/**
 * Updates the tier of a file in the metadata.
 * This function adjusts the usage counters accordingly.
 *
 * @param path     The original file path.
 * @param new_tier The new tier where the file resides.
 */
void cache_policy_update_tier(const char* path, cache_tier_t new_tier)
{
    if (path == NULL) return;
    pthread_mutex_lock(&g_cacheMutex);
    std::string key(path);
    auto it = g_fileMetaMap.find(key);
    if (it != g_fileMetaMap.end()) {
        uint64_t size = it->second.size;
        // Subtract from old tier counter
        if (it->second.current_tier == CACHE_TIER_PM) {
            if (g_pmUsedBytes >= size)
                g_pmUsedBytes -= size;
        } else if (it->second.current_tier == CACHE_TIER_SSD) {
            if (g_ssdUsedBytes >= size)
                g_ssdUsedBytes -= size;
        }
        // Set new tier and add to corresponding counter
        it->second.current_tier = new_tier;
        if (new_tier == CACHE_TIER_PM) {
            g_pmUsedBytes += size;
        } else if (new_tier == CACHE_TIER_SSD) {
            g_ssdUsedBytes += size;
        }
    }
    pthread_mutex_unlock(&g_cacheMutex);
}

/**
 * Sets the open state (open/closed) of a file in the metadata.
 *
 * @param path    The original file path.
 * @param is_open true if file is now open; false if closed.
 */
void cache_policy_set_open_state(const char* path, bool is_open)
{
    if (path == NULL) return;
    pthread_mutex_lock(&g_cacheMutex);
    std::string key(path);
    auto it = g_fileMetaMap.find(key);
    if (it != g_fileMetaMap.end()) {
        it->second.is_open = is_open;
    }
    pthread_mutex_unlock(&g_cacheMutex);
}

/**
 * Retrieves the current tier of a file.
 *
 * @param path The original file path.
 * @return The current cache tier, or CACHE_TIER_UNKNOWN if not found.
 */
cache_tier_t cache_policy_get_tier(const char* path)
{
    if (path == NULL) return CACHE_TIER_UNKNOWN;
    pthread_mutex_lock(&g_cacheMutex);
    std::string key(path);
    cache_tier_t tier = CACHE_TIER_UNKNOWN;
    auto it = g_fileMetaMap.find(key);
    if (it != g_fileMetaMap.end()) {
        tier = it->second.current_tier;
    }
    pthread_mutex_unlock(&g_cacheMutex);
    return tier;
}

/**
 * Selects a victim file for eviction from PM.
 * The policy here is to choose the closed file in PM with the lowest access count.
 *
 * @return A std::string containing the victim file's path, or an empty string if none found.
 */
std::string cache_policy_select_victim_for_eviction()
{
    pthread_mutex_lock(&g_cacheMutex);
    std::string victim = "";
    uint64_t min_access = UINT64_MAX;
    for (auto &entry : g_fileMetaMap) {
        if (entry.second.current_tier == CACHE_TIER_PM && (!entry.second.is_open)) {
            if (entry.second.access_count < min_access) {
                min_access = entry.second.access_count;
                victim = entry.first;
            }
        }
    }
    pthread_mutex_unlock(&g_cacheMutex);
    return victim;
}

/**
 * Removes a file from the metadata tracking.
 *
 * @param path The file path to remove.
 */
void cache_policy_remove_file(const char* path)
{
    if (path == NULL) return;
    pthread_mutex_lock(&g_cacheMutex);
    std::string key(path);
    auto it = g_fileMetaMap.find(key);
    if (it != g_fileMetaMap.end()) {
        uint64_t size = it->second.size;
        // Adjust usage counters based on the current tier.
        if (it->second.current_tier == CACHE_TIER_PM) {
            if (g_pmUsedBytes >= size)
                g_pmUsedBytes -= size;
        } else if (it->second.current_tier == CACHE_TIER_SSD) {
            if (g_ssdUsedBytes >= size)
                g_ssdUsedBytes -= size;
        }
        g_fileMetaMap.erase(it);
    }
    pthread_mutex_unlock(&g_cacheMutex);
}

/**
 * Checks PM usage and evicts files from PM if necessary.
 * This function selects a victim file (using cache_policy_select_victim_for_eviction)
 * and updates its tier from PM to SSD (if SSD has space) or to PFS as a fallback.
 *
 * @return 0 if eviction succeeded or not needed, non-zero if eviction failed.
 */
int cache_policy_evict_if_needed()
{
    pthread_mutex_lock(&g_cacheMutex);
    int ret = 0;
    // If PM usage is within capacity, nothing to do.
    if (g_pmUsedBytes <= g_pmCapacityBytes) {
        pthread_mutex_unlock(&g_cacheMutex);
        return 0;
    }
    // Select a victim for eviction.
    std::string victim = cache_policy_select_victim_for_eviction();
    if (victim.empty()) {
        ret = -1;
        pthread_mutex_unlock(&g_cacheMutex);
        return ret;
    }
    // Find the victim's metadata and update its tier.
    auto it = g_fileMetaMap.find(victim);
    if (it != g_fileMetaMap.end()) {
        uint64_t size = it->second.size;
        // Remove the file's usage from PM.
        if (g_pmUsedBytes >= size)
            g_pmUsedBytes -= size;
        // Try to move it to SSD if space permits.
        if (g_ssdUsedBytes + size <= g_ssdCapacityBytes) {
            it->second.current_tier = CACHE_TIER_SSD;
            g_ssdUsedBytes += size;
        } else {
            // Otherwise, mark it as residing in PFS.
            it->second.current_tier = CACHE_TIER_PFS;
        }
    }
    pthread_mutex_unlock(&g_cacheMutex);
    return ret;
}

/**
 * Returns the current usage (in bytes) of PM and SSD.
 *
 * @param used_pm_bytes   [out] Pointer to store current PM usage.
 * @param used_ssd_bytes  [out] Pointer to store current SSD usage.
 */
void cache_policy_get_usage_bytes(uint64_t* used_pm_bytes, uint64_t* used_ssd_bytes)
{
    if (used_pm_bytes == NULL || used_ssd_bytes == NULL)
        return;
    pthread_mutex_lock(&g_cacheMutex);
    *used_pm_bytes = g_pmUsedBytes;
    *used_ssd_bytes = g_ssdUsedBytes;
    pthread_mutex_unlock(&g_cacheMutex);
}
