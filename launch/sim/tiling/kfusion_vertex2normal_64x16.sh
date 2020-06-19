#!/bin/bash

./main -d 640,480 -w 64 -c 1,data/kfusion/vertex2normal_out.bin $* src/kernels/kfusion_vertex2normal_64x16.sas
