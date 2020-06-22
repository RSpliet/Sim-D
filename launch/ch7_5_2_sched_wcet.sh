#!/bin/bash

BENCHMARKS=('cnn_convolution' \
'cnn_maxpool' \
'cnn_relu' \
'fft' \
'kfusion_depth2vertex' \
'kfusion_halfSampleRobustImage' \
'kfusion_track' \
'kfusion_vertex2normal' \
'mriq_computePhiMag' \
'mriq_computeQ' \
'spmv' \
'srad2' \
'srad_reduce' \
'srad' \
'stencil')

echo "Benchmark & Unconstr. & Perf. paral. & Single-buf & \\multicol{3}{c}{SP as access} & \\multicol{3}{c}{SP as execute}\\\\"
echo " & avg & wcet & wcet & avg & wcet & \\% diff & avg & wcet & \\% diff\\\\"
echo "\\hline"

for script in ${BENCHMARKS[@]}; do
	echo -n "${script} & & "
	res=`launch/wcet/${script}.sh -3 -P 5 | grep 'WCET' | cut -b 6- | sed -e 's/^[ \t]*//'`
	let best=0
	let sb=0
	let wxs=0
	let wex=0
	while read -ra res_array ; do
		let b=${res_array[0]}
		let best=${best}+${b}
		let b=${res_array[1]}
		let sb=${sb}+${b}
		let b=${res_array[2]}
		let wxs=${wxs}+${b}
		let b=${res_array[3]}
		let wex=${wex}+${b}
	done <<< "$res"
	echo "${best} & ${sb} & & ${wxs} & & & ${wex} & \\\\"
done



