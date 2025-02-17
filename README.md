# High Velocity AI Cache (Developing)

High Velocity AI Cache (HVAC) is a high-performance caching system designed for AI workloads, initially built on [summit](https://www.olcf.ornl.gov/olcf-resources/compute-systems/summit/). This repo is built on [NCSU ARC](https://arcb.csc.ncsu.edu/~mueller/cluster/arc/).  It extends the original [HVAC](https://ieeexplore.ieee.org/document/9912705) by integrating Persistent Memory (PMEM) to optimize data movement and storage efficiency. This work aims to push the boundaries of zero-copy storage and metadata-driven file management, enhancing AI training and inference workloads.

## Key Enhancements
1. Persistent Memory Integration: Extends HVAC to support PMEM for faster, non-volatile caching.

2. Zero-Copy I/O Redirection: Uses LD_PRELOAD to intercept file operations and optimize read/write workflows. (TBC)

3. Metadata-Driven File Management: Maintains file access metadata in PMEM, enabling efficient file migration and lookup.

4. Dynamic Storage Tiering: Implements an adaptive mechanism to store frequently accessed data on PMEM while offloading less critical data to SSDs.

## System Design
The system operates with a client-server architecture, where:

- **Clients** intercept file system calls (open, read, write, close) and redirect them to PMEM when applicable.
- **Metadata Management** ensures seamless file access and maintains mapping between logical and physical storage locations.
- **Server** (Optional for Distributed Mode) coordinates metadata and data consistency across multiple nodes.


## Compile and Run
### Prerequisites
To build and run HVAC with PMEM support, ensure the following dependencies are installed:

#### Software
- [Mercury](https://mercury-hpc.github.io/) (for RPC communication)
- [Log4C](https://log4c.sourceforge.net/) (for debugging and logging)
- CMake (version >= 3.16)
- GCC (version >= 9.1.0)
- libfabric (if using distributed mode)

#### Hardware
- Persistent Memory
- Node-local Storage

### Compilation
on ARC (Recommended Setup)
1. Pull HVAC from Gitlab into $HVAC_SOURCE_DIR directory
2. create build_dir
3. cd build_dir
4. source $HVAC_SOURCE_DIR/build.sh 

### Run
*NOTE - the Server component will not work in this build because the mercury build is based on a libfabric build that does not support VERBS


On other systems currently building this is a little bit challenging -

1. Grab and build mercury
2. Add the mercury package config to your pkg_config_path
	/gpfs/alpine/stf008/scratch/cjzimmer/HVAC/mercury-install/lib/pkgconfig
	[cjzimmer@login3.summit pkgconfig]$ export PKG_CONFIG_PATH=$PKG_CONFIG_PATH:$PWD

3. module load log4c
4. module load gcc/9.1.0 <- Specific for now
5. export CC=/sw/summit/gcc/9.1.0-alpha+20190716/bin/gcc 
6. cd into your build directory
7. cmake ../HVAC
8. Build should work - Currently hvac_server is wired up to listen on an ib port
9. The client connection code is a work in progress

