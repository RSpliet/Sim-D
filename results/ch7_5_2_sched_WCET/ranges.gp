#!/usr/bin/gnuplot
# Very simple Gantt Chart
# Demonstrate using timecolumn(N,format) to plot time data from 
# multiple columns
#
set encoding utf8;
set terminal postscript eps enhanced colour dashed 'Helvetica' 14 size 16cm,8.5cm;
set output 'wcet_sched.eps';
set format x "%0.1f M"

set yrange [-1:]
set mxtics 4
set ytics nomirror
set grid x y
set lmargin at screen 0.18
set xlabel "Cycles"
set ylabel "Benchmarks"
unset border

set datafile separator ";";
set style arrow 1 heads filled size screen 0.011, 90 fixed lt 3 lw 6 lc rgb "#e06060"
set style arrow 2 nohead filled size screen 0.02, 15 fixed lt 3 lw 3 lc "#36b344"
set style arrow 3 nohead filled size screen 0.02, 15 fixed lt 3 lw 3 lc "#290d80"
set style arrow 4 heads filled size screen 0.004, 90 fixed lt 0 lw 5 lc "#36b344"
set style arrow 5 heads filled size screen 0.004, 90 fixed lt 0 lw 5 lc "#290d80"
set style line 6 lw 3 lc "#36b344" pt 7
set style line 7 lw 3 lc "#290d80" pt 7
set multiplot;

set size 1, 0.7;
set origin 0,0.3;

set xrange [0:1.6]
set xtics scale 2, 0.75
set key bottom right Left reverse width -5

set arrow from 0,-1 rto 0,8.5 nohead lw 0.5
set arrow from 0,-1 rto 1.6,0 nohead lw 0.5

plot "rawdata_filtered.csv" using ($3/1e6):($0):(($4-$3)/1e6):(0.0):yticlabel(1) title "WCET range" with vector as 1, \
     "rawdata_filtered.csv" using ($2/1e6):($0) title "Unconstrained (measured)" with points lc black lt 2, \
     "rawdata_filtered.csv" using ($6/1e6):($0-0.4):(0):(0.4) title "Scratchpad as access (WCET)" with vectors as 2, \
     "rawdata_filtered.csv" using ($5/1e6):($0-0.2) title "Scratchpad as access (measured)" with points ls 6, \
     "rawdata_filtered.csv" using ($9/1e6):($0-0):(0):(0.4) title "Scratchpad as compute (WCET)" with vectors as 3, \
     "rawdata_filtered.csv" using ($8/1e6):($0+0.2) title "Scratchpad as compute (measured)" with points ls 7, \
     "rawdata_filtered.csv" using ($5/1e6):($0-0.2):(($6-$5)/1e6):(0) notitle with vectors as 4, \
     "rawdata_filtered.csv" using ($8/1e6):($0+0.2):(($9-$8)/1e6):(0) notitle with vectors as 5;

unset arrow

set xrange [15:95]
set xtics scale 2, 0.75 20, 10
unset key
unset title
set size 1, 0.3
set origin 0,0;

set arrow from 15,-1. rto 0,2.5 nohead lw 0.5
set arrow from 20,-1 rto 75,0 nohead lw 0.5
set arrow from 15, -1 rto 1.3, 0.25 nohead lw 0.5
set arrow from 16.3, -0.75 rto 2.4, -0.5 nohead lw 0.5
set arrow from 18.7, -1.25 rto 1.3, 0.25 nohead lw 0.5

plot "rawdata_high.csv" using ($3/1e6):($0):(($4-$3)/1e6):(0.0):yticlabel(1) title "WCET range" with vector as 1, \
     "rawdata_high.csv" using ($2/1e6):($0) title "Unconstrained (measured)" with points lc black lt 2, \
     "rawdata_high.csv" using ($6/1e6):($0-0.4):(0):(0.4) title "Scratchpad as access (WCET)" with vectors as 2, \
     "rawdata_high.csv" using ($5/1e6):($0-0.2) title "Scratchpad as access (measured)" with points ls 6, \
     "rawdata_high.csv" using ($9/1e6):($0-0):(0):(0.4) title "Scratchpad as compute (WCET)" with vectors as 3, \
     "rawdata_high.csv" using ($8/1e6):($0+0.2) title "Scratchpad as compute (measured)" with points ls 7, \
     "rawdata_high.csv" using ($5/1e6):($0-0.2):(($6-$5)/1e6):(0) notitle with vectors as 4, \
     "rawdata_high.csv" using ($8/1e6):($0+0.2):(($9-$8)/1e6):(0) notitle with vectors as 5;

