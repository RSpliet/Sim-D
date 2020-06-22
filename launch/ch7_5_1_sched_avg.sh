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

hdr="config"
for script in "${BENCHMARKS[@]}"; do
	hdr+=",${script}"
done

echo ${hdr}

for schedOpt in "-s pairwise_wg" "-s no_parallel_dram_compute" "-s no_parallel_dram_sp"; do
	echo -n "\"${schedOpt}\""

	for script in "${BENCHMARKS[@]}"; do
		res=`launch/sim/${script}.sh -3 -P 5 ${schedOpt} | grep 'Program latency' | cut -b 30-45 | sed -e 's/^[ \t]*//'`
		let c=0
		while read -r l ; do
			let b=${l}
			let c=${c}+${b}
		done <<< "$res"
		echo -n ",${c}"
	done
	echo ""
done



