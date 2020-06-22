#!/usr/bin/gnuplot
set style data histogram
set style histogram rowstacked
set style fill solid
set boxwidth 0.5;
set encoding utf8;
set ylabel "instructions/ld+st+atom byte" rotate by 90 right;
set terminal postscript eps enhanced colour dashed 'Helvetica' 14 size 16cm,9.5cm
set output 'hist_insmix.eps'
set grid front noxtics x2tics lw 2
set xtics rotate by 45 right
set key outside right noinvert
set yrange [0:1]
plot "kernel_stats.txt" using ($2/$11):xtic(1) title "ALU", '' using ($3/$11) title "FPU", '' using ($7/$11) title "Trigo", '' using ($4/$11) title "Ld/St global", '' using ($5/$11) title "Ld/St local", '' using ($6/$11) title "Ld/st const",'' using ($10/$11) title "Flow ctrl",'' using ($8/$11) title "Atomic int", '' using ($9/$11) title "Atomic fp32"

set output 'hist_insmix_noconst.eps'
plot "kernel_stats.txt" using ($2/($11-$6)):xtic(1) title "ALU", '' using ($3/($11-$6)) title "FPU", '' using ($7/($11-$6)) title "Trigo", '' using ($4/($11-$6)) title "Ld/St global", '' using ($5/($11-$6)) title "Ld/St local", '' using ($10/($11-$6)) title "Flow ctrl",'' using ($8/($11-$6)) title "Atomic int", '' using ($9/($11-$6)) title "Atomic fp32"

set output 'hist_insperbyte.eps'
set ylabel "instructions/ld+st+atom byte" rotate by 90 right;
set yrange [0:25]
set grid ytics
set ytics 5
set x2tics("440" 12)
plot "kernel_stats.txt" using ($2/(($4)*4)):xtic(1) title "ALU", '' using ($3/(($4)*4)) title "FPU", '' using ($7/(($4)*4)) title "Trigo", '' using ($5/(($4)*4)) title "Ld/St local", '' using ($10/(($4)*4)) title "Flow ctrl"
