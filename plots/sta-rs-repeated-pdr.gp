set terminal pngcairo size 1600,1000 enhanced font "Sans,14"
set output "sta-rs-repeated-pdr.png"

set title "Application packet delivery: STA-RS and comparison methods\n10 runs per method and scenario; identical monitoring enabled" offset 0,1
set xlabel "Movement scenario" offset 0,-0.5
set ylabel "Application packet delivery ratio (%)"

set xrange [-0.55:2.55]
set yrange [0:108]
set ytics 20
set xtics ("Stationary" 0, "Moving away" 1, "Moving toward source" 2)
set grid ytics back lc rgb "#dddddd"
set border 3
set tics nomirror
set style fill solid 0.85 border -1
set bars 0.6

set lmargin at screen 0.09
set rmargin at screen 0.97
set bmargin at screen 0.23
set tmargin at screen 0.87

set key at screen 0.53,0.12 center center horizontal maxrows 2 \
    samplen 2 spacing 1.3 font ",12"

set label 1 \
    "Bar height = mean; whiskers = observed minimum–maximum across 10 runs (not confidence intervals)." \
    at screen 0.53,0.035 center font ",11"

plot \
 "pdr-comparison.dat" using ($1-0.30):3:(0.16) with boxes lc rgb "#0072b2" title "AODV + monitoring", \
 "" using ($1-0.10):6:(0.16) with boxes lc rgb "#e69f00" title "Strongest signal", \
 "" using ($1+0.10):9:(0.16) with boxes lc rgb "#cc79a7" title "Fixed weights", \
 "" using ($1+0.30):12:(0.16) with boxes lc rgb "#009e73" title "STA-RS", \
 "" using ($1-0.30):3:4:5 with yerrorbars pt 0 lw 1.5 lc rgb "#333333" notitle, \
 "" using ($1-0.10):6:7:8 with yerrorbars pt 0 lw 1.5 lc rgb "#333333" notitle, \
 "" using ($1+0.10):9:10:11 with yerrorbars pt 0 lw 1.5 lc rgb "#333333" notitle, \
 "" using ($1+0.30):12:13:14 with yerrorbars pt 0 lw 1.5 lc rgb "#333333" notitle, \
 "" using ($1-0.30):($5+2):(sprintf("%.2f", $3)) with labels font ",11" notitle, \
 "" using ($1-0.10):($8+2):(sprintf("%.2f", $6)) with labels font ",11" notitle, \
 "" using ($1+0.10):($11+2):(sprintf("%.2f", $9)) with labels font ",11" notitle, \
 "" using ($1+0.30):($14+2):(sprintf("%.2f", $12)) with labels font ",11" notitle
