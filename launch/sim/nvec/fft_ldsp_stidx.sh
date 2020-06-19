#!/bin/bash

./main -d 128,1024 -c 1,data/fft/out.bin $* src/kernels/fft_ldsp_stidx.sas
