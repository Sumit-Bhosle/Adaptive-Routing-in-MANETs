set terminal pngcairo size 1400,1000 enhanced font "Sans,12"
set output "candidate-decisions.png"
set datafile separator ","
set datafile columnheaders

set multiplot layout 3,1 title \
    "Relay preferences from recorded observations — moving relay 1, relay 2 at 25 m"

set xrange [0:30]
set yrange [-0.3:2.3]
set ytics ("None" 0, "Relay 1" 1, "Relay 2" 2)
set ylabel "Preferred relay"
set grid
set key outside right
set arrow 1 from 10,graph 0 to 10,graph 1 \
    nohead dt 2 lc rgb "#666666"

set title "Strongest received signal"
plot "candidate-decisions.csv" \
    using 1:14 \
    with steps lw 2 lc rgb "#0072b2" title "Strongest signal"

set title "Fixed weights: equal importance to quality and stability"
plot "candidate-decisions.csv" \
    using 1:15 \
    with steps lw 2 lc rgb "#d55e00" title "Fixed weights"

set title "Adaptive weights: stability gains importance as weakening increases"
set xlabel "Simulation time (s); dashed line marks movement starting"
plot "candidate-decisions.csv" \
    using 1:16 \
    with steps lw 2 lc rgb "#009e73" title "Adaptive weights"

unset multiplot
