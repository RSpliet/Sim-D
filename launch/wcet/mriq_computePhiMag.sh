#!/bin/bash

./wcet -d 2048,1 -w 1024 $* src/kernels/mriq_computePhiMag.sas
