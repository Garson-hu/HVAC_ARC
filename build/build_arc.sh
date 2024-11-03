#!/bin/bash


export BBPATH=/mnt/local
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


cmake -DCMAKE_C_COMPILER=/opt/ohpc/pub/compiler/gcc/9.4.0/bin/gcc -DCMAKE_CXX_COMPILER=/opt/ohpc/pub/compiler/gcc/9.4.0/bin/g++ .. -DDEBUG_HU=0

make -j4

