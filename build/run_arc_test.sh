#!/bin/bash
#SBATCH -p cascade
#SBATCH --nodes=1                     # Request 4 nodes
##SBATCH -q debug
#SBATCH -t 02:00:00
#SBATCH -J hvac_job
#SBATCH -o logs/%x-%j.out

rm *.0
# Set the environment variables
export BBPATH=/mnt/local/ghu4
export HVAC_SERVER_COUNT=1
export HVAC_LOG_LEVEL=800
export RDMAV_FORK_SAFE=1
export VERBS_LOG_LEVEL=4
export PKG_CONFIG_PATH=$PKG_CONFIG_PATH:/home/ghu4/hvac/log4c-1.2.4/install/lib/pkgconfig
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/home/ghu4/hvac/log4c-1.2.4/install/lib
export PATH=/home/ghu4/hvac/mercury-2.0.1/build/bin:$PATH
export LD_LIBRARY_PATH=/home/ghu4/hvac/rlibrary/mercury2.0.1/lib:$LD_LIBRARY_PATH
export C_INCLUDE_PATH=/home/ghu4/hvac/rlibrary/mercury2.0.1/include:$C_INCLUDE_PATH
export CPLUS_INCLUDE_PATH=/home/ghu4/hvac/rlibrary/mercury2.0.1/include:$CPLUS_INCLUDE_PATH
export PKG_CONFIG_PATH=$PKG_CONFIG_PATH:/home/ghu4/hvac/rlibrary/mercury2.0.1/lib/pkgconfig

export OMP_NUM_THREADS=32
export KMP_BLOCKTIME=1
export KMP_AFFINITY="granularity=fine,compact,1,0"
export HDF5_USE_FILE_LOCKING=FALSE

# Set the data directory
# export HVAC_DATA_DIR=/mnt/beegfs/ghu4/hvac/cosmoUniverse_2019_05_4parE_tf_v2_mini/train/

## Load the required modules
# module load tensorflow/intel-2.2.0-py37
# module list

echo "CUDA_VISIBLE_DEVICES: $CUDA_VISIBLE_DEVICES"
# Start HVAC servers on every allocated node
echo "Starting HVAC servers on allocated nodes..."

srun -N1 -n1 -c1 /home/ghu4/hvac/GHU_HVAC/build/src/hvac_server 1 &

# Wait for the HVAC servers to start
# sleep 10

# Run the distributed training
set -x
export LD_PRELOAD=/home/ghu4/hvac/GHU_HVAC/build/src/libhvac_client.so 
srun -N1 -n1 -c2 /home/ghu4/hvac/GHU_HVAC/build/tests/basic_test

