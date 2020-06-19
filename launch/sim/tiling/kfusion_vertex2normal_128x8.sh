#!/bin/bash

./main -d 640,480 -w 128 -c 1,data/kfusion/vertex2normal_out.bin $* src/kernels/kfusion_vertex2normal_128x8.sas
