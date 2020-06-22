BENCHMARKS=('cnn_convolution' \
'unroll/cnn_convolution_unroll' \
'mriq_computeQ' \
'unroll/mriq_computeQ_unroll2' \
'unroll/mriq_computeQ_unroll4' \
)

echo "config,unconstrained,pairwise,sp_as_compute,sp_as_access,WCET_LB,WCET_UB,WCET_sp_as_compute,WCET_sp_as_access"

for script in "${BENCHMARKS[@]}"; do
	echo -n "$(basename ${script})"
	for schedOpt in " " "-s pairwise_wg" "-s no_parallel_compute_sp" "-s no_parallel_dram_sp"; do
		res=`launch/sim/${script}.sh -3 -P 5 ${schedOpt} | grep 'Program latency' | cut -b 30-45 | sed -e 's/^[ \t]*//'`
		let c=0
		while read -r l ; do
			let b=${l}
			let c=${c}+${b}
		done <<< "$res"
		echo -n ",${c}"
	done
		
	res=`launch/wcet/${script}.sh -3 -P 5 | grep 'WCET' | cut -b 6- | sed -e 's/^[ \t]*//'`
	let wxs=0
	let wex=0
	let wlb=0
	let wub=0
	while read -ra res_array ; do
		let b=${res_array[0]}
		let wlb=${wlb}+${b}
		let b=${res_array[1]}
		let wub=${wub}+${b}
		let b=${res_array[2]}
		let wxs=${wxs}+${b}
		let b=${res_array[3]}
		let wex=${wex}+${b}
	done <<< "$res"
	echo "${wlb},${wub},${wxs},${wex}"
done
