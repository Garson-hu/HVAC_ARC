#!/bin/bash
#SBATCH -w c31
##SBATCH --ntasks-per-node=1
#SBATCH -p a6000
#SBATCH --nodes=1
#SBATCH -c 2
#SBATCH -t 1-00:00:00
#SBATCH -J hvac_job
#SBATCH -o logs/CF_HVAC-%j.out

# rm *.0
# rm *.1
# Set the environment variables
echo "Allocated Nodes: $SLURM_NODELIST"
if [ ! -d "/mnt/local/ghu4/train_cache" ]; then
    echo "/mnt/local/ghu4/train_cache does not exist. Creating it now..."
    mkdir -p /mnt/local/ghu4/train_cache
fi

# ! MPI recall
# export OMPI_MCA_mpi_debug=10
# export BBPATH=/mnt/fsdax/ghu4/train_cache
export BBPATH=/mnt/local/ghu4/train_cache
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

 #! Need change here
export HVAC_SERVER_COUNT=1
export HVAC_DATA_DIR=/mnt/beegfs/ghu4/hvac/cosmoUniverse_2019_05_4parE_tf_v2_mini/train_61440/train/
# export HVAC_DATA_DIR=/mnt/beegfs/ghu4/hvac/cosmoUniverse_TEST/train/

# Start HVAC servers on every allocated node
echo "Starting HVAC servers on allocated nodes..."
set -x
# -oversubscribe --map-by slot --bind-to core -display-map -display-allocation -H $(scontrol show  hostname | tr "\n" ",") -np 16

mpirun -N 1 /home/ghu4/hvac/GHU_HVAC/build/src/hvac_server $HVAC_SERVER_COUNT &

# --mca btl tcp,self --mca btl_tcp_if_include eth0 -np 4
# --mca btl_tcp_if_include ib0
#  --mca pml ucx #for IB, not working since conda openmpi not compiled with --enable-mt, see https://github.com/openucx/ucx/issues/5284
# --gpus-per-node=4 --ntasks-per-gpu=1
mpirun -N 1 /home/ghu4/hvac/benchmark/cosmoflow-benchmark-master/command_CF_HVAC.sh

#finally, kill all servers (not very graceful)
# mpirun killall -9 /home/ghu4/hvac/GHU_HVAC/build/src/hvac_server
