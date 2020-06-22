#!/usr/bin/gnuplot
set encoding utf8;
set xlabel "MRI-Q computeQ variant";
set ylabel "Run-time (cycles)";
set terminal postscript eps enhanced colour dashed 'Helvetica' 14 size 7.8cm,7.5cm;
set output 'mriq_unroll.eps';

set style line 1 linetype 1 dashtype 1 linecolor rgb "#332288" linewidth 2.2 pointtype 1
set style line 2 linetype 2 dashtype 2 linecolor rgb "#66AACC" linewidth 2.2 pointtype 2
set style line 3 linetype 3 dashtype 3 linecolor rgb "#117733" linewidth 2.2 pointtype 3
set style line 4 linetype 4 dashtype 4 linecolor rgb "#DDCC77" linewidth 2.2 pointtype 4
set style line 5 linetype 5 dashtype 5 linecolor rgb "#CC6677" linewidth 2.2 pointtype 5
set style line 6 linetype 7 dashtype 6 linecolor rgb "#AA4499" linewidth 2.2 pointtype 7
set style line 7 linetype 7 dashtype 7 linecolor rgb "#44AA99" linewidth 2.2 pointtype 7
set style line 8 linetype 7 dashtype 8 linecolor rgb "#882255" linewidth 2.2 pointtype 7
set style fill solid 0.3

set format y "%10.0f"

set xrange [0.5:3.5];
set yrange [0:160000000];
set key tmargin;

xcoord(i,n) =  i*ClusterSize + n

ClusterSize = 5
set boxwidth 1.0 abs
category = 'mriq\_computeQ'
set xtics rotate by -60 ('Base' 1, 'Unroll(2)' 2, 'Unroll(4)' 3);

set ytics 10000000
set grid ytics;
set datafile separator ";";
unset key;

plot for [n=2:9] "mriq_unroll_design_space_transpose.csv" using ($0+1):(column(n)) title columnhead(n) w linespoints lt rgb "#332288", \
for [n=10:17] "mriq_unroll_design_space_transpose.csv" using ($0+1):(column(n)) title columnhead(n) w linespoints lt rgb "#66AACC", \
for [n=18:25] "mriq_unroll_design_space_transpose.csv" using ($0+1):(column(n)) title columnhead(n) w linespoints lt rgb "#117733";
