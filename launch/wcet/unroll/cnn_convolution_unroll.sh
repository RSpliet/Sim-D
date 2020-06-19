#!/bin/bash

./wcet -d 218,14336 -w 32 $* src/kernels/cnn_convolution_unrollx.sas
