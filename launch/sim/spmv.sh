#!/bin/bash

./main -d 11948,1 -e 0.05\% -c 6,data/spmv/dst_vector.csv $* src/kernels/spmv.sas
