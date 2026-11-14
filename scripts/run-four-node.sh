#!/usr/bin/env bash
set -euo pipefail
project=$(cd "$(dirname "$0")/.." && pwd)
ns3_dir="${NS3_DIR:-$HOME/Projects/ns-3.46}"
ln -sfn "$project/simulations/aodv-four-node.cc" "$ns3_dir/scratch/aodv-four-node.cc"
cmake -S "$ns3_dir" -B "$ns3_dir/cmake-cache"
cmake --build "$ns3_dir/cmake-cache" --target scratch_aodv-four-node --parallel 2
sim="$ns3_dir/build/scratch/ns3.46-aodv-four-node-debug"
out="$project/results/udp-four-node"
mkdir -p "$out"
trial=$(mktemp -d "$out/trial-XXXXXX")
mkdir "$trial/stationary" "$trial/moving"
run_case() {
    (cd "$trial/$1" && "$sim" --moveRelay="$2" | tee run.log)
}
source_routes() {
    awk '/^Node:/ {source=($2=="0;"); t=$4} source && $1=="10.0.0.4" {print t,$2,$4,$6}' "$1"
}
run_case stationary 0
gateway=$(source_routes "$trial/stationary/aodv.routes" | awk '$1=="+8s," && $3=="UP" && $4==2 {print $2}')
case "$gateway" in
    10.0.0.2) relay=1 ;;
    10.0.0.3) relay=2 ;;
    *) echo "No active two-hop route found at 8 s. Inspect $trial/stationary/aodv.routes"; exit 1 ;;
esac
run_case moving "$relay"
echo "Results: $trial"
for scenario in stationary moving; do
    echo "$scenario: snapshot time / next hop / route status / hops"
    source_routes "$trial/$scenario/aodv.routes"
    tail -3 "$trial/$scenario/run.log"
done
awk '$1=="UDP_RX" && $2>=10 {printf "First observed delivery after relay movement: %.6f s after t=10\n",$2-10; found=1; exit} END {if (!found) print "No post-movement arrival observed"}' "$trial/moving/run.log"
