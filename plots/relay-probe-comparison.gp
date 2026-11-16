set terminal pngcairo size 1400,850 enhanced font "Sans,12"
set output "relay-probe-comparison.png"

set title "Signals from two relays observed at node 0"
set xlabel "Simulation time (s)"
set ylabel "Received signal power (dBm)"

set xrange [0:30]
set yrange [-90:-45]
set grid
set key outside below center horizontal
set border linewidth 1.2

set arrow 1 from 10,graph 0 to 10,graph 1 \
    nohead dt 2 lc rgb "#666666"
set label 1 "Relay 1 starts moving" at 10.4,-48

plot "relay1-signal.dat" using 1:2 \
       with points pt 7 ps 0.8 lc rgb "#d55e00" \
       title "Relay 1 - moving at 5 m/s after 10 s", \
     "relay2-signal.dat" using 1:2 \
       with points pt 5 ps 0.6 lc rgb "#0072b2" \
       title "Relay 2 - stationary at 5 m"
