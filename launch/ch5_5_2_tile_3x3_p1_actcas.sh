#!/bin/bash

echo "Config & words & \\multicolumn{2}{c}{Cycles} & Activate & Burst read\\\\"

for (( y=1; y<=128; y <<= 1))
do
	x=$((1024 / y))
	w=$((x+2))
	h=$((y+2))
	wr=$((w*h))
	
	min=1000000
	max=0
	energymin=100000000
	energymax=0
	actmax=0
	rdmax=0

	imm=`src/mc/mc -s 0,${w},1026,${h},1000,0 -S 2>&1 | grep "delay\|energy\|Activate\|Read"`
	max=`echo "${imm}" | grep "delay" | cut -c 34-43 | xargs -E0 printf %u`
	min=`echo "${imm}" | grep "delay" | cut -c 24-33 | xargs -E0 printf %u`
#	energymax=`echo "${imm}" | grep "energy" | cut -c 34-43 | xargs -E0 printf %g`
#	energymin=`echo "${imm}" | grep "energy" | cut -c 24-33 | xargs -E0 printf %g`
	actmax=`echo "${imm}" | grep "Activate" | cut -c 34-43 | xargs -E0 printf %u`
	rdmax=`echo "${imm}" | grep "Read/write" | cut -c 34-43 | xargs -E0 printf %u`

	energymax=`bc <<< "scale=1;(${energymax}+50)/1000"`
	energymin=`bc <<< "scale=1;(${energymin}+50)/1000"`
	
	echo "(${x},${y}) & ${wr} & ${min} & ${max} & ${actmax} & ${rdmax}"

done

