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
#### on ARC (Recommended Setup)
1. Load required modules:
```
module load log4c
module load mercury
module laod gcc12 (optional)
```
2. Clone HVAC source code:
```
git clone https://github.com/Garson-hu/HVAC_ARC.git
```
3. Go to build_dir
```
mkdir -p $HVAC_SOURCE_DIR/build && cd $HVAC_SOURCE_DIR/build
```
4. Run the build script
``` 
./build_arc.sh 
```
#### On other system (TBC)

### Run

1. Import all the required environment variables:
```
export BBPATH=/YOUR_NODE_LOCAL_STORAGE_PATH/
export HVAC_LOG_LEVEL=800
export RDMAV_FORK_SAFE=1
export VERBS_LOG_LEVEL=4
export HVAC_SERVER_COUNT=YOUR_SERVER_COUNT (Single node: 1, Distributed: number of nodes)
export HVAC_DATA_DIR=/YOUR_TRAINING_SET_PATH/

```

2. Launch the server and client
```
mpirun -N 1 /home/ghu4/hvac/GHU_HVAC/build/src/hvac_server $HVAC_SERVER_COUNT &
mpirun -N 1 /home/ghu4/hvac/benchmark/cosmoflow-benchmark-master/command_CF_HVAC.sh

```

## Future work
- Work on Devdax instead of fsdax