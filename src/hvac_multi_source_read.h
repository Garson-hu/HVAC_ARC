/**
 * Author: Guangxing Hu
 * Affiliation: North Carolina State University
 * Email: ghu4@ncsu.edu
 * 
 * This header file declares functions for performing multi-source read operations.
 * For each read request, the client concurrently issues I/O requests to both the PM and SSD tiers via RPC.
 * The first successful response is used, while slower or failed responses are ignored.
 *
 * This mechanism ensures that the read operation returns with the lowest latency,
 * even if the data exists only in SSD.
 */

#ifndef HVAC_MULTI_SOURCE_READ_HPP
#define HVAC_MULTI_SOURCE_READ_HPP

#include <cstddef>    // for size_t
#include <sys/types.h> // for off_t

namespace hvac {

/**
 * Performs a multi-source read operation.
 *
 * This function concurrently issues read requests (via RPC) to both the PM and SSD tiers.
 * It waits for the first valid response and returns the number of bytes read.
 *
 * @param fd      File descriptor for the tracked file.
 * @param buf     Buffer to store the read data.
 * @param count   Number of bytes to read.
 * @param offset  Offset in the file from where to read.
 *
 * @return The number of bytes read on success, or -1 on failure.
 */
int ms_read(int fd, void* buf, size_t count, off_t offset);

} // namespace hvac

#endif // HVAC_MULTI_SOURCE_READ_HPP
