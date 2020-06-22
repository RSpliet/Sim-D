#!/usr/bin/gnuplot
set encoding utf8;
set xlabel "Benchmark";
set ylabel "Speed-up (normalised to (64,3,6))";
set terminal postscript eps enhanced colour dashed 'Helvetica' 14 size 23cm,15cm;
set output 'arch_design_space_sp32.eps';

set style line 1 linetype 1 dashtype 1 linecolor rgb "#332288" linewidth 2.2 pointtype 1
set style line 2 linetype 2 dashtype 2 linecolor rgb "#66AACC" linewidth 2.2 pointtype 2
set style line 3 linetype 3 dashtype 3 linecolor rgb "#117733" linewidth 2.2 pointtype 3
set style line 4 linetype 4 dashtype 4 linecolor rgb "#DDCC77" linewidth 2.2 pointtype 4
set style line 5 linetype 5 dashtype 5 linecolor rgb "#CC6677" linewidth 2.2 pointtype 5
set style line 6 linetype 7 dashtype 6 linecolor rgb "#AA4499" linewidth 2.2 pointtype 7
set style line 7 linetype 7 dashtype 7 linecolor rgb "#44AA99" linewidth 2.2 pointtype 7
set style line 8 linetype 7 dashtype 8 linecolor rgb "#882255" linewidth 2.2 pointtype 7

set key left nobox title "(SP-units, decode stages, execute stages)"
set xrange [0:14];
set yrange [0.95:3.5];
set xtics rotate by -60 ('cnn\_convolution' 0, 'cnn\_maxpool' 1, 'cnn\_relu' 2, 'fft' 3, 'kfusion\_depth2vertex' 4, 'kfusion\_half'."\n\n".'SampleRobustImage' 5, 'kfusion\_track' 6, 'kfusion\_vertex2normal' 7, 'mriq\_computePhiMag' 8, 'mriq\_computeQ' 9, "spmv" 10, "srad2" 11, 'srad\_reduce' 12, "srad" 13, "stencil" 14);
set ytics 0.1
set grid xtics ytics;
set datafile separator ";";

#plot for [n=2:25] "design_space_sp32_transpose.csv" using ($0):($9/(column(n))) title columnhead(n) w linespoints;
plot for [n=2:9] "design_space_sp32_transpose.csv" using ($0):($9/(column(n))) title columnhead(n) w linespoints lt rgb "#332288", \
for [n=10:17] "design_space_sp32_transpose.csv" using ($0):($9/(column(n))) title columnhead(n) w linespoints lt rgb "#66AACC", \
for [n=18:25] "design_space_sp32_transpose.csv" using ($0):($9/(column(n))) title columnhead(n) w linespoints lt rgb "#117733";
