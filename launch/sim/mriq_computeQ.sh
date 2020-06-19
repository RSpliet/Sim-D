#!/bin/bash

./main -d 262144,1 -c 4,data/mri-q/qR_out.bin -c 5,data/mri-q/qI_out.bin -e 0.02 $* src/kernels/mriq_computeQ.sas
