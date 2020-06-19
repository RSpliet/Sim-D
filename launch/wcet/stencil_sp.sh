#!/bin/bash

./wcet -d 126,3840 -w 128 $* src/kernels/stencil_sp.sas
