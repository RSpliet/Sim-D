#!/bin/bash

./main -d 640,480 -w 32 -o 6,kfusion_track.bin $* src/kernels/kfusion_track.sas 
./src/util/cmp_kfusion_track kfusion_track.bin data/kfusion/track_out.bin 307200
