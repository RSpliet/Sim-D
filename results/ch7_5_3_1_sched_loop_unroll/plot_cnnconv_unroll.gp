#!/usr/bin/gnuplot
set encoding utf8;
set xlabel "CNN convolution variant";
set ylabel "Run-time (Cycles)";
set terminal postscript eps enhanced colour dashed 'Helvetica' 14 size 7.8cm,7.5cm;
set key rmargin;

set style line 1 linetype 1 dashtype 1 linecolor rgb "#332288" linewidth 2.2 pointtype 1
set style line 2 linetype 2 dashtype 2 linecolor rgb "#66AACC" linewidth 2.2 pointtype 2
set style line 3 linetype 3 dashtype 3 linecolor rgb "#117733" linewidth 2.2 pointtype 3
set style line 4 linetype 4 dashtype 4 linecolor rgb "#DDCC77" linewidth 2.2 pointtype 4
set style line 5 linetype 5 dashtype 5 linecolor rgb "#CC6677" linewidth 2.2 pointtype 5
set style line 6 linetype 7 dashtype 6 linecolor rgb "#AA4499" linewidth 2.2 pointtype 7
set style line 7 linetype 7 dashtype 7 linecolor rgb "#44AA99" linewidth 2.2 pointtype 7
set style line 8 linetype 7 dashtype 8 linecolor rgb "#882255" linewidth 2.2 pointtype 7

set xrange [-0.5:1.5];
set yrange [0:32000000];
set xtics rotate by -60 ('Base' 0, 'Unroll(7)' 1);
set ytics 2000000
set grid ytics;
set datafile separator ";";
set format y "%8.0f"

set output 'cnnconv_unroll.eps';
plot for [n=2:9] "cnnconv_unroll_design_space_transpose.csv" using ($0):(column(n)) title columnhead(n) w linespoints lt rgb "#332288", \
for [n=10:17] "cnnconv_unroll_design_space_transpose.csv" using ($0):(column(n)) title columnhead(n) w linespoints lt rgb "#66AACC", \
for [n=18:25] "cnnconv_unroll_design_space_transpose.csv" using ($0):(column(n)) title columnhead(n) w linespoints lt rgb "#117733";

set output 'cnnconv_unroll_design_space.eps';
set ylabel "Speed-up (normalised to (64,3,6))";
set xrange [-0.5:1.5];
set yrange [0.95:2];
set ytics 0.25
unset format y

plot for [n=2:9] "cnnconv_unroll_design_space_transpose.csv" using ($0):($9/(column(n))) title columnhead(n) w points lt rgb "#332288", \
for [n=10:17] "cnnconv_unroll_design_space_transpose.csv" using ($0):($9/(column(n))) title columnhead(n) w points lt rgb "#66AACC", \
for [n=18:25] "cnnconv_unroll_design_space_transpose.csv" using ($0):($9/(column(n))) title columnhead(n) w points lt rgb "#117733";
