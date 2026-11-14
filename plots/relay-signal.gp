set terminal pngcairo size 1100,650
set output "relay-signal.png"

set title "Signal received by relay 1 as it moves away from node 0"
set xlabel "Simulation time (s)"
set ylabel "Received signal power (dBm)"
set xrange [0:30]
set yrange [-90:-45]
set grid
set key bottom left

set arrow 1 from 10,graph 0 to 10,graph 1 nohead dt 2 lc rgb "#555555"
set label 1 "Movement starts" at 10.4,-48

plot "signal.dat" using 1:2 with points pt 7 ps 0.9 lc rgb "#1769aa" \
     title "Node 0 to relay 1, received frames"
