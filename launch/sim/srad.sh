#!/bin/bash

./main -d 504,458 -w 512 -c 8,data/srad/d_c.bin -c 4,data/srad/d_dN.bin -c 5,data/srad/d_dS.bin -c 6,data/srad/d_dE.bin -c 7,data/srad/d_dW.bin $* src/kernels/srad.sas
