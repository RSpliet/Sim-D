#!/bin/bash

BENCHMARKS=('tiling/kfusion_vertex2normal_128x8' \
'tiling/kfusion_vertex2normal_64x16' \
'kfusion_vertex2normal'
)

echo "Tiling & \\multicol{2}{c|}{Read LID} & \\multicol{2}{c|}{Write LID} & SP idx & Avg & DRAM & \\multicol{2}{c}{WCET}\\\\"
echo "Config. & min & max & min & max & cycs & cycs & cycs & SP as access & SP as execute\\\\"
echo "\\hline"

for script in "${BENCHMARKS[@]}"; do
	echo -n "$(basename ${script}) & & & & & & "

	res=`launch/sim/${script}.sh -3 -P 5 | grep -e 'Program latency' -e 'DRAM active'`
	let c=0
	let d=0
	while read -r l ; do
		let b=`echo "${l}" | cut -b 30-39 | sed -e 's/^[ \t]*//'`
		
		if [[ ${l} == *"Program latency"* ]]; then
			let c=${c}+${b}
		else
			let d=${d}+${b}
		fi
	done <<< "$res"
	
	res=`launch/wcet/${script}.sh -3 -P 5 | grep 'WCET' | cut -b 6- | sed -e 's/^[ \t]*//'`
	let wxs=0
	let wex=0
	while read -ra res_array ; do
		let b=${res_array[2]}
		let wxs=${wxs}+${b}
		let b=${res_array[3]}
		let wex=${wex}+${b}
	done <<< "$res"
	echo "${c} & ${d} & ${wxs} & ${wex} \\\\"
done
