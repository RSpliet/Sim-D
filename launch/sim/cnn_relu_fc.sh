#!/bin/bash

./main -d 4096,1 -c 3,data/cnn_relu/out_large.csv $* src/kernels/cnn_relu_fc.sas
