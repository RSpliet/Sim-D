#!/bin/bash

./main -d 128,1024 -c 1,data/fft/out.bin $* src/kernels/fft_ld2vec_stsp.sas
