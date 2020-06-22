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

for decOpt in 1 3; do
	for execOpt in 3 4 5 6; do
		if [ ${decOpt} -eq 3 ]; then
			dec="-3 "
		else
			dec=""
		fi
		echo -n "\"($1,${decOpt},${execOpt})\""

		for script in "${BENCHMARKS[@]}"; do
			res=`launch/sim/${script}.sh ${dec}-P ${execOpt} | grep 'Program latency' | cut -b 30-45 | sed -e 's/^[ \t]*//'`
			let c=0
			while read -r l ; do
				let b=${l}
				let c=${c}+${b}
			done <<< "$res"
			echo -n ",${c}"
		done
		echo ""
	done
done



