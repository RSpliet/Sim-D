#!/bin/bash

./wcet -d 4096,1 -w 1024 $* src/kernels/cnn_relu_fc.sas
