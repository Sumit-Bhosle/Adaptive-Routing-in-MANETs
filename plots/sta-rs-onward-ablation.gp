set terminal pngcairo size 1400,1200 enhanced font "Sans,12"
set output "sta-rs-onward-ablation.png"

set multiplot layout 3,1 title \
    "STA-RS: contribution of onward-link verification\nConfigured onward routes; same monitoring configuration; one seed/run"

set lmargin 14
set rmargin 4
set grid
set key outside below center horizontal

set title "Application packet reception"
set xlabel "Packet sequence number"
set ylabel "Reception"
set xrange [-0.5:29.5]
set yrange [-0.2:1.2]
set ytics ("Missing" 0, "Received" 1)

plot "require-0-delivery.dat" using 1:2 with points \
    pt 6 ps 1.3 lc rgb "#d55e00" title "Onward check disabled: 13/30", \
     "require-1-delivery.dat" using 1:2 with points \
    pt 2 ps 1.0 lc rgb "#0072b2" title "Onward check enabled: 28/30"

set title "Cumulative application packets received"
set xlabel "Simulation time (s)"
set ylabel "Received packets (count)"
set xrange [0:30]
set yrange [0:30]
set ytics autofreq
set arrow 1 from 15,graph 0 to 15,graph 1 \
    nohead dt 2 lc rgb "#666666"

plot "require-0-cumulative.dat" using 1:2 with steps \
    lw 2 lc rgb "#d55e00" title "Check disabled", \
     "require-1-cumulative.dat" using 1:2 with steps \
    lw 2 lc rgb "#0072b2" title "Check enabled; relay changes at 15 s"

set title "Relay 1 onward-evidence age at source decision times"
set xlabel "Simulation time (s)"
set ylabel "Evidence age (s)"
set xrange [10:18]
set yrange [0:6]

plot "require-0-evidence.dat" using 1:2 with points \
    pt 6 ps 1.3 lc rgb "#d55e00" title "Check disabled", \
     "require-1-evidence.dat" using 1:2 with points \
    pt 2 ps 1.0 lc rgb "#0072b2" title "Check enabled", \
     2 with lines dt 2 lw 2 lc rgb "#333333" title "Freshness limit: 2 s"

unset multiplot
