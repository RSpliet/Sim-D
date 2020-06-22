#!/bin/bash

echo "\\begin{tabular}{l|r||r@{ - }l r@{ - }l||r|r|r|r|r|r|r|r}"
echo " & Net & \\multicolumn{4}{c||}{Sim-D (min-max)} & \\multicolumn{7}{c|}{Predator (n-burst fixed size, cycles)} & Best\\\\"
echo "Config & wrds & \\multicolumn{2}{c}{Cycles} & \\multicolumn{2}{c||}{Energy(nJ)}& n = 1 & 2 & 4 & 8 & 16 & 32 & 64 & nJ\\\\"
echo "\\hline"

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

	imm=`src/mc/mc -s 0,${w},1026,${h},1000,0 -S 2>&1 | grep "delay\|energy"`
	max=`echo "${imm}" | grep "delay" | cut -c 34-43 | xargs -E0 printf %u`
	min=`echo "${imm}" | grep "delay" | cut -c 24-33 | xargs -E0 printf %u`
	energymax=`echo "${imm}" | grep "energy" | cut -c 34-43 | xargs -E0 printf %g`
	energymin=`echo "${imm}" | grep "energy" | cut -c 24-33 | xargs -E0 printf %g`

	energymax=`bc <<< "scale=1;(${energymax}+50)/1000"`
	energymin=`bc <<< "scale=1;(${energymin}+50)/1000"`
	
	predator=`src/stridegen/stridegen -l -c ${h} ${w} 1026 2>/dev/null`

	echo "(${x},${y}) & ${wr} & ${min} & ${max} & ${energymin} & ${energymax} & ${predator}"

done

echo "\\end{tabular}"

