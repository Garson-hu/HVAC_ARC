#!/bin/bash

module reset
#module load PrgEnv-gnu 
module load mercury cmake libfabric
module unload darshan-runtime 

export HVAC_SERVER_COUNT=1
export HVAC_LOG_LEVEL=800
export RDMAV_FORK_SAFE=1
export VERBS_LOG_LEVEL=4
export BBPATH=/mnt/bb/$USER

export http_proxy=http://proxy.ccs.ornl.gov:3128/
export https_proxy=http://proxy.ccs.ornl.gov:3128/

export CC=/lustre/orion/proj-shared/stf008/hvac/hsoon/GCC-9.1.0/bin/gcc
export CXX=/lustre/orion/proj-shared/stf008/hvac/hsoon/GCC-9.1.0/bin/c++
#export PKG_CONFIG_PATH=$PKG_CONFIG_PATH:/lustre/orion/proj-shared/stf008/hvac/hsoon/GCC-9.1.0/
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/lustre/orion/proj-shared/stf008/hvac/hsoon/GCC-9.1.0/lib64

export PKG_CONFIG_PATH=$PKG_CONFIG_PATH:/lustre/orion/proj-shared/stf008/hvac/log4c-1.2.4/install/lib/pkgconfig
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/lustre/orion/proj-shared/stf008/hvac/log4c-1.2.4/install/lib


cmake ../

make -j4

