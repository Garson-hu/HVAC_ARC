#!/bin/bash
#SBATCH -w c30
#SBATCH -p a6000
#SBATCH -c 1
#SBATCH -t 1-00:00:00
#SBATCH -J pure_CF
#SBATCH -o logs/pure_CF-%j.out


##SBATCH -p rtx4060ti16g
##SBATCH --ntasks-per-node=1
##SBATCH --nodes=1

echo "Allocated Nodes: $SLURM_NODELIST"
# Set the environment variables

# ! MPI recall
# export OMPI_MCA_mpi_debug=10
export PATH=/opt/ohpc/pub/mpi/openmpi4-gnu12/4.1.6/bin:$PATH
export LD_LIBRARY_PATH=/opt/ohpc/pub/mpi/openmpi4-gnu12/4.1.6/lib:$LD_LIBRARY_PATH

export RDMAV_FORK_SAFE=1
export VERBS_LOG_LEVEL=4
export PKG_CONFIG_PATH=$PKG_CONFIG_PATH:/home/ghu4/hvac/log4c-1.2.4/install/lib/pkgconfig
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/home/ghu4/hvac/log4c-1.2.4/install/lib
export PATH=/home/ghu4/hvac/mercury-2.0.1/build/bin:$PATH
export LD_LIBRARY_PATH=/home/ghu4/hvac/rlibrary/mercury2.0.1/lib:$LD_LIBRARY_PATH
export C_INCLUDE_PATH=/home/ghu4/hvac/rlibrary/mercury2.0.1/include:$C_INCLUDE_PATH
export CPLUS_INCLUDE_PATH=/home/ghu4/hvac/rlibrary/mercury2.0.1/include:$CPLUS_INCLUDE_PATH
export PKG_CONFIG_PATH=$PKG_CONFIG_PATH:/home/ghu4/hvac/rlibrary/mercury2.0.1/lib/pkgconfig


echo "Starting HVAC servers on allocated nodes..."
set -x
# -oversubscribe --map-by slot --bind-to core -display-map -display-allocation -H $(scontrol show  hostname | tr "\n" ",") -np 16


# --mca btl tcp,self --mca btl_tcp_if_include eth0 -np 4
# --mca btl_tcp_if_include ib0
#  --mca pml ucx #for IB, not working since conda openmpi not compiled with --enable-mt, see https://github.com/openucx/ucx/issues/5284
# mpirun -np 1 /home/ghu4/hvac/benchmark/cosmoflow-benchmark-master/command_pure_CF.sh
# --oversubscribe -H localhost:4
mpirun -N 1 /home/ghu4/hvac/rlibrary/miniconda3/envs/hvac/bin/python3 /home/ghu4/hvac/benchmark/cosmoflow-benchmark-master/train.py

#finally, kill all servers (not very graceful)
# mpirun killall -9 /home/ghu4/hvac/GHU_HVAC/build/src/hvac_server
