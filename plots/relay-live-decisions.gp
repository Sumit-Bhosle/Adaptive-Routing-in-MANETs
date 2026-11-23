set terminal pngcairo size 1400,1200 enhanced font "Sans,12"
set output "relay-live-decisions.png"

set multiplot layout 4,1 title \
    "Live relay preferences and AODV route snapshots — moving relay 1"

set xrange [0:30]
set yrange [-0.25:2.25]
set ytics ("None" 0, "Relay 1" 1, "Relay 2" 2)
set grid
set lmargin 12
set rmargin 4
set key outside below center horizontal
set ylabel "Relay"
set xlabel "Simulation time (s)"

set arrow 1 from 10,graph 0 to 10,graph 1 \
    nohead dt 2 lc rgb "#777777"

set title "Strongest-signal preference — observation only"
plot "live-decisions.dat" using 1:2 with steps \
    lw 2 lc rgb "#0072b2" title "Strongest signal"

set title "Fixed-weight preference — observation only"
plot "live-decisions.dat" using 1:3 with steps \
    lw 2 lc rgb "#d55e00" title "Fixed quality/stability weights"

set title "Our adaptive preference — observation only"
plot "live-decisions.dat" using 1:4 with steps \
    lw 2 lc rgb "#009e73" title "Signal-trend-aware adaptive selection"

set title "Standard AODV — source routing-table snapshots"
set xlabel "Simulation time (s); dashed line marks movement starting"
plot "aodv-route-snapshots.dat" using 1:2 with points \
    pt 7 ps 1.1 lc rgb "#6a3d9a" \
    title "AODV next hop; None = no UP destination route"

unset multiplot
