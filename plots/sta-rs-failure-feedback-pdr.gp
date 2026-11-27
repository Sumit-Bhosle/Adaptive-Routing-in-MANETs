set terminal pngcairo size 1600,1300 enhanced font "Sans,13"
set output "sta-rs-failure-feedback-pdr.png"

set multiplot layout 2,1 title \
    "STA-RS evaluation: delivery performance and contribution of failure feedback"

set xrange [-0.55:2.55]
set yrange [0:108]
set ytics 20
set xtics ("Stationary" 0, "Moving away" 1, "Moving toward source" 2)
set xlabel "Movement scenario"
set ylabel "Application packet delivery ratio (%)"
set grid ytics back lc rgb "#dddddd"
set border 3
set tics nomirror
set style fill solid 0.85 border -1
set bars 0.6
set lmargin 11
set rmargin 3
set bmargin 6
set tmargin 4
set key outside below center horizontal font ",11"

set title "Current methods: common monitoring and failure-reporting mechanisms enabled\n10 runs per scenario; custom selectors use configured onward routes"

plot \
 "pdr.dat" using ($1-0.30):2:(0.16) with boxes lc rgb "#0072b2" title "AODV + monitoring", \
 "" using ($1-0.10):5:(0.16) with boxes lc rgb "#e69f00" title "Strongest signal", \
 "" using ($1+0.10):8:(0.16) with boxes lc rgb "#cc79a7" title "Fixed weights", \
 "" using ($1+0.30):14:(0.16) with boxes lc rgb "#009e73" title "STA-RS + failure feedback", \
 "" using ($1-0.30):2:3:4 with yerrorbars pt 0 lc rgb "#333333" notitle, \
 "" using ($1-0.10):5:6:7 with yerrorbars pt 0 lc rgb "#333333" notitle, \
 "" using ($1+0.10):8:9:10 with yerrorbars pt 0 lc rgb "#333333" notitle, \
 "" using ($1+0.30):14:15:16 with yerrorbars pt 0 lc rgb "#333333" notitle, \
 "" using ($1-0.30):($4+2):(sprintf("%.2f", $2)) with labels font ",10" notitle, \
 "" using ($1-0.10):($7+2):(sprintf("%.2f", $5)) with labels font ",10" notitle, \
 "" using ($1+0.10):($10+2):(sprintf("%.2f", $8)) with labels font ",10" notitle, \
 "" using ($1+0.30):($16+2):(sprintf("%.2f", $14)) with labels font ",10" notitle

set title "Contribution of failure feedback within STA-RS\nBars: mean; whiskers: observed minimum–maximum across 10 runs, not confidence intervals"

plot \
 "pdr.dat" using ($1-0.13):11:(0.22) with boxes lc rgb "#d55e00" title "STA-RS without failure feedback", \
 "" using ($1+0.13):14:(0.22) with boxes lc rgb "#009e73" title "STA-RS with failure feedback", \
 "" using ($1-0.13):11:12:13 with yerrorbars pt 0 lc rgb "#333333" notitle, \
 "" using ($1+0.13):14:15:16 with yerrorbars pt 0 lc rgb "#333333" notitle, \
 "" using ($1-0.13):($13+2):(sprintf("%.2f", $11)) with labels font ",11" notitle, \
 "" using ($1+0.13):($16+2):(sprintf("%.2f", $14)) with labels font ",11" notitle

unset multiplot
