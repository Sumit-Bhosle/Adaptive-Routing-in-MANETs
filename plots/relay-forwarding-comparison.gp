set terminal pngcairo size 1400,1200 enhanced font "Sans,12"
set output "relay-forwarding-comparison.png"
set datafile missing "NaN"

set multiplot layout 3,1 title \
    "Moving-relay experiment: AODV vs adaptive prototype\nPrototype uses configured relay-to-destination routes; one seed/run"

set lmargin 14
set rmargin 4
set grid
set key outside below center horizontal

set title "Observed relay forwarding events"
set xlabel "Simulation time (s); movement starts at 10 s"
set ylabel "Forwarding relay"
set xrange [0:30]
set yrange [0.7:2.3]
set ytics ("Relay 1" 1, "Relay 2" 2)

set arrow 1 from 10,graph 0 to 10,graph 1 \
    nohead dt 2 lc rgb "#777777"

plot "aodv-forwarding.dat" using 1:2 with points \
       pt 6 ps 1.4 lc rgb "#0072b2" title "Standard AODV", \
     "adaptive-forwarding.dat" using 1:2 with points \
       pt 2 ps 1.0 lc rgb "#d55e00" title "Adaptive prototype"

unset arrow 1
set title "Packet reception by sequence number"
set xlabel "Packet sequence number"
set ylabel "Reception"
set xrange [-0.5:29.5]
set yrange [-0.2:1.2]
set ytics ("Missing" 0, "Received" 1)

plot "aodv-delivery.dat" using 1:2 with points \
       pt 6 ps 1.4 lc rgb "#0072b2" title "Standard AODV: 29/30", \
     "adaptive-delivery.dat" using 1:2 with points \
       pt 2 ps 1.0 lc rgb "#d55e00" title "Adaptive prototype: 30/30"

set title "Delivery delay around the transition — received packets only"
set xlabel "Packet sequence number"
set ylabel "End-to-end delay (ms)"
set xrange [11.5:19.5]
set yrange [0:12]
set ytics autofreq

plot "aodv-delivery.dat" using 1:4 with points \
       pt 6 ps 1.4 lc rgb "#0072b2" title "Standard AODV", \
     "adaptive-delivery.dat" using 1:4 with points \
       pt 2 ps 1.0 lc rgb "#d55e00" title "Adaptive prototype"

unset multiplot
