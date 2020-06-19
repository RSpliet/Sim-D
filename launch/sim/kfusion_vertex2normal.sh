#!/bin/bash

./main -d 640,480 -w 32 -c 1,data/kfusion/vertex2normal_out.bin $* src/kernels/kfusion_vertex2normal_32x32.sas
