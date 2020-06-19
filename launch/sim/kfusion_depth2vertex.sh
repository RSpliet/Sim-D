#!/bin/bash

./main -d 640,480 -w 64 -c 1,data/kfusion/depth2vertex_out.bin $* src/kernels/kfusion_depth2vertex.sas
