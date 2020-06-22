#!/bin/bash

for (( y=1; y<=4096; y += 1))
do
	min=1000000
	max=0
	energymax=1000000000
	energymin=0

	imm=`./mc -s 0,${y},${y},1,1000,0 -S -n 4000 2>&1 | grep "delay\|energy"`
	max=`echo "${imm}" | grep "delay" | cut -c 34-43 | xargs -E0 printf %u`
	min=`echo "${imm}" | grep "delay" | cut -c 24-33 | xargs -E0 printf %u`
	energymax=`echo "${imm}" | grep "energy" | cut -c 34-43 | xargs -E0 printf %g`
	energymin=`echo "${imm}" | grep "energy" | cut -c 24-33 | xargs -E0 printf %g`

	echo "${y} ${min} ${max} ${energymin} ${energymax}"
done


