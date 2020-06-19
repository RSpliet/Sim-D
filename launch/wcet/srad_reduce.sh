#!/bin/bash

./wcet -d 114958,1 -w 1024 $* src/kernels/srad_reduce.sas
./wcet -d 113,1 -w 1024 $* src/kernels/srad_reduce_invoc_2.sas
#./main -d 113,1 -i 2,data/srad/reduce_constants_1024.bin -w 1024 -i 0,run/d_I_red_1.bin -i 1,run/d_sums2_red_1.bin -c 0,data/srad/d_sums_res.bin -c 1,data/srad/d_sums2_res.bin -e 0.3\% $* src/kernels/srad_reduce.sas

# Even for the OpenCL kernel, the chosen reduction method makes a big
# difference in the final result. The magic results that we stuck in data/srad/
# are taken from Sim-D, and are ~0.3% off from the Rodinia results.

#rm run/d_sums2_red_1.bin
#rm run/d_I_red_1.bin
