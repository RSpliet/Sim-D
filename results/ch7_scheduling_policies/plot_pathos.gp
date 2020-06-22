#!/usr/bin/gnuplot
set encoding utf8;
set xlabel "Time (cycle)";
set terminal postscript eps enhanced colour dashed 'Helvetica' 14 size 16cm,4cm;
set output 'wcet_pathos.eps';

set style increment default;
set view map scale 1;
set style data lines;

set style line 1 linetype 1 dashtype 1 linecolor rgb "#332288" linewidth 2.2 pointtype 1
set style line 2 linetype 2 dashtype 2 linecolor rgb "#66AACC" linewidth 2.2 pointtype 2
set style line 3 linetype 3 dashtype 3 linecolor rgb "#117733" linewidth 2.2 pointtype 3
set style line 4 linetype 4 dashtype 4 linecolor rgb "#DDCC77" linewidth 2.2 pointtype 4
set style line 5 linetype 5 dashtype 5 linecolor rgb "#CC6677" linewidth 2.2 pointtype 5
set style line 6 linetype 7 dashtype 6 linecolor rgb "#AA4499" linewidth 2.2 pointtype 7
set style line 7 linetype 7 dashtype 7 linecolor rgb "#44AA99" linewidth 2.2 pointtype 7
set style line 8 linetype 7 dashtype 8 linecolor rgb "#882255" linewidth 2.2 pointtype 7

set xrange [-0.5:700.5];
set yrange [0:2];
set key tmargin;
set lmargin at screen 0.055;
set rmargin at screen 0.82;

set style line 100 lt 1 lc rgb "gray" lw 0;
set link y2
set xtics 100
set ytics scale 0 out ("WG0" 0.5, "WG1" 1.5)
set y2tics in ("" 1)
set grid noxtics noytics y2tics;

set datafile separator ",";
set palette maxcolors 6
set palette defined (0 "white", 1 "red", 2 "green", 3 "seagreen", 4 "blue", 5 "black")
set cbtics ("Idle" 0, "Exec" 1, "DRAM (BCRT)" 2, "DRAM (WCRT)" 3, "SP" 4, "Blocked" 5)

#set multiplot layout 3,1

set pm3d corners2color c1
set arrow from 56,1.2 rto 684,1.2 heads size screen 0.01, 90 fixed front lt 3 lw 3 lc rgb "#606060"
splot "pathos.txt" using 1:2:3 notitle with pm3d;



