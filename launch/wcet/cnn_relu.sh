#!/bin/bash

./wcet -d 256,256 -w 256 $* src/kernels/cnn_relu.sas
