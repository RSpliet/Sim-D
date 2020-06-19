#!/bin/bash

./wcet -d 11948,1 -w 1024 $* src/kernels/spmv.sas
