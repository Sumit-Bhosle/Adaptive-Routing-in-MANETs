set terminal pngcairo size 1400,1200 enhanced font "Sans,12"
set output "relay-live-observation.png"

set multiplot layout 3,1 title \
    "AODV baseline: gradual relay movement and observed delivery"

set lmargin 12
set rmargin 4
set xrange [0:30]
set grid
set key outside below center horizontal
set xlabel "Simulation time (s)"

set arrow 1 from 10,graph 0 to 10,graph 1 \
    nohead dt 2 lc rgb "#777777"

set yrange [-85:-75]
set ylabel "Received power (dBm)"

set title "At source: relay 1 weakens; relay 2 remains stationary"
plot "relay1-source.dat" using 1:2 with points \
       pt 7 ps 0.7 lc rgb "#d55e00" title "Relay 1", \
     "relay2-source.dat" using 1:2 with points \
       pt 5 ps 0.6 lc rgb "#0072b2" title "Relay 2"

set title "At destination: observations of the onward links"
plot "relay1-destination.dat" using 1:2 with points \
       pt 7 ps 0.7 lc rgb "#d55e00" title "Relay 1", \
     "relay2-destination.dat" using 1:2 with points \
       pt 5 ps 0.6 lc rgb "#0072b2" title "Relay 2"

set autoscale y
set yrange [0:*]
set ylabel "End-to-end delay (ms)"
set title "Received UDP packets only; lost packets have no delay sample"
set xlabel "Simulation time (s); dashed line marks movement starting"

plot "stationary-delay.dat" using 1:2 with points \
       pt 6 ps 1.2 lc rgb "#0072b2" title "Stationary AODV: 30/30", \
     "moving-delay.dat" using 1:2 with points \
       pt 7 ps 0.7 lc rgb "#d55e00" title "Moving AODV: 29/30"

unset multiplot
