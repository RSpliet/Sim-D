#!/bin/bash

./wcet -d 262144,1 -w 1024 $* src/kernels/mriq_computeQ_unroll2.sas
