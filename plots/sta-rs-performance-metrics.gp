set terminal pngcairo size 1700,1400 enhanced font "Sans,13"

array names[5] = ["AODV + monitoring", "Strongest + feedback", \
                 "Fixed + feedback", "STA-RS without failure feedback", \
                 "STA-RS with failure feedback"]

set style line 1 lc rgb "#0072b2" lw 2 pt 6
set style line 2 lc rgb "#e69f00" lw 2 pt 8
set style line 3 lc rgb "#cc79a7" lw 2 pt 4
set style line 4 lc rgb "#d55e00" lw 2 pt 2
set style line 5 lc rgb "#009e73" lw 3 pt 7

set border 3
set tics nomirror
set grid ytics back lc rgb "#dddddd"
set lmargin 12
set rmargin 4
set tmargin 4
set bmargin 7
set key outside below center horizontal maxrows 2 font ",11"
set style fill solid 0.85 border -1
set bars 0.5

set output "sta-rs-delay-and-monitoring.png"
set multiplot layout 2,1 title \
    "STA-RS performance and monitoring cost — 10 runs per configuration"

set xrange [-0.6:2.6]
set xtics ("Stationary" 0, "Moving away" 1, "Moving toward source" 2)
set xlabel "Movement scenario"
set yrange [0:*]
set ylabel "Mean end-to-end delay (ms)"
set title "Packets sent from 10 s onward; received packets only\nPoints: mean of per-run means; whiskers: observed run range, not confidence intervals"

plot for [m=1:5] "delay.dat" \
    using ($1+(m-3)*0.15):(column(2+3*(m-1))):\
          (column(3+3*(m-1))):(column(4+3*(m-1))) \
    with yerrorbars ls m ps 1.4 title names[m]

set ylabel "Monitoring UDP payload (kB/run)"
set title "Payload accepted by sockets over 30 s; 1 kB = 1000 bytes\nIncludes probes, validation and feedback; excludes headers, AODV, ARP, ACKs and retries"

plot for [m=1:5] "monitoring.dat" \
    using ($1+(m-3)*0.17):(column(2+3*(m-1))):(0.14) \
    with boxes ls m title names[m], \
    for [m=1:5] "monitoring.dat" \
    using ($1+(m-3)*0.17):(column(2+3*(m-1))):\
          (column(3+3*(m-1))):(column(4+3*(m-1))) \
    with yerrorbars pt 0 lc rgb "#333333" notitle

unset multiplot

set terminal pngcairo size 1700,1700 enhanced font "Sans,13"
set output "sta-rs-goodput-over-time.png"
set multiplot layout 3,1 title \
    "Delivered application UDP payload rate — mean across 10 runs\n128-byte payload includes SeqTsHeader; identical results overlap"

set xrange [0:30]
set xtics 0,5,30
set yrange [0:*]
set xlabel "Simulation time (s)"
set ylabel "Application goodput (kbit/s)"
set arrow 1 from 10,graph 0 to 10,graph 1 \
    nohead dt 2 lc rgb "#777777"
set bmargin 7

array scenarios[3] = ["stationary", "away", "toward"]
array titles[3] = ["Stationary: dashed line marks matched 10 s analysis boundary", \
                  "Moving away: movement starts at 10 s", \
                  "Moving toward source: movement starts at 10 s"]

do for [s=1:3] {
    set title titles[s]
    plot for [m=1:5] sprintf("goodput-%s.dat", scenarios[s]) \
        using (($1+$2)/2):(column(m+2)) \
        with linespoints ls m dt m ps 0.7 title names[m]
}

unset multiplot
set output
