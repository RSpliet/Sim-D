#!/usr/bin/gnuplot
set encoding utf8;
set xlabel "Benchmark";
set ylabel "Speed-up (normalised to unrestricted)";
set terminal postscript eps enhanced colour dashed 'Helvetica' 14 size 16cm,8cm;
set output 'arch_sched_policy_sp32.eps';

set style line 1 linetype 1 dashtype 1 linecolor rgb "#332288" linewidth 2.2 pointtype 1
set style line 2 linetype 2 dashtype 2 linecolor rgb "#66AACC" linewidth 2.2 pointtype 2
set style line 3 linetype 3 dashtype 3 linecolor rgb "#117733" linewidth 2.2 pointtype 3
set style line 4 linetype 4 dashtype 4 linecolor rgb "#DDCC77" linewidth 2.2 pointtype 4
set style line 5 linetype 5 dashtype 5 linecolor rgb "#CC6677" linewidth 2.2 pointtype 5
set style line 6 linetype 7 dashtype 6 linecolor rgb "#AA4499" linewidth 2.2 pointtype 7
set style line 7 linetype 7 dashtype 7 linecolor rgb "#44AA99" linewidth 2.2 pointtype 7
set style line 8 linetype 7 dashtype 8 linecolor rgb "#882255" linewidth 2.2 pointtype 7
set style fill solid 0.3

set xrange [0.5:75.5];
set yrange [0:1.1];
set key tmargin horizontal;
set rmargin 0.98

ClusterSize = 5
set boxwidth 1.0 abs
category = 'cnn\_convolution cnn\_maxpool cnn\_relu fft kfusion\_depth2vertex kfusion\_half... kfusion\_track kfusion\_vertex2normal mriq\_computePhiMag mriq\_computeQ spmv srad2 srad\_reduce srad stencil'
set xtics 300 offset -2
set for [i=1:15] xtics rotate by -45 add (word(category,i) 4+(i-1)*ClusterSize)

xcoord(i,n) =  i*ClusterSize + n-1

set ytics 0.1
set grid ytics;
set datafile separator ";";

plot for [n=3:5] "sched_policy_sp32_ddr3200_transpose.csv" using (xcoord($0,n)):($2/(column(n))) title columnhead(n) w boxes;
