#!/bin/bash

# LD_PRELOAD=/home/ghu4/hvac/GHU_HVAC/tests/ld_preload_new.so
# LD_PRELOAD=/home/ghu4/hvac/GHU_HVAC/build/src/libhvac_client.so
# -m cProfile -o cProfile_results_HVAC_1024_d
time LD_PRELOAD=/home/ghu4/hvac/GHU_HVAC/build/src/libhvac_client.so /home/ghu4/hvac/rlibrary/miniconda3/envs/hvac/bin/python3 /home/ghu4/hvac/benchmark/cosmoflow-benchmark-master/train.py
# time  LD_PRELOAD=/home/ghu4/hvac/GHU_HVAC/build/src/libhvac_client.so /home/ghu4/hvac/GHU_HVAC/build/tests/basic_test

echo DONE `hostname`
