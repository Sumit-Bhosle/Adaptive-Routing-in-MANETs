#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
sim="$HOME/Projects/ns-3.46/build/scratch/ns3.46-relay-failure-feedback-debug"
source_file="$project_dir/simulations/relay-failure-feedback.cc"

[[ -x "$sim" ]] || {
  echo "Simulation executable missing: $sim"
  exit 1
}

mkdir -p "$project_dir/results/relay-failure-feedback"
batch=$(mktemp -d \
  "$project_dir/results/relay-failure-feedback/repeated-XXXXXX")

cp "$source_file" "$batch/relay-failure-feedback-source.cc"
sha256sum "$source_file" "$sim" > "$batch/build-sha256.txt"

printf 'scenario,configuration,run,selector,rule,fast_failure,relative_directory\n' \
  > "$batch/manifest.csv"

completed=0

for scenario in stationary away toward; do
  case "$scenario" in
    stationary) move=0; toward=0 ;;
    away)       move=1; toward=0 ;;
    toward)     move=1; toward=1 ;;
  esac

  for config in aodv strongest fixed stars-original stars-fast; do
    case "$config" in
      aodv)           selector=0; rule=2; fast=1 ;;
      strongest)      selector=1; rule=0; fast=1 ;;
      fixed)          selector=1; rule=1; fast=1 ;;
      stars-original) selector=1; rule=2; fast=0 ;;
      stars-fast)     selector=1; rule=2; fast=1 ;;
    esac

    for run in {1..10}; do
      relative="$scenario/$config/run-$run"
      out="$batch/$relative"
      mkdir -p "$out"

      args=(
        "--rngRun=$run"
        "--adaptiveForwarding=$selector"
        "--selectionRule=$rule"
        "--fastFailure=$fast"
        "--moveRelay=$move"
        "--towardSource=$toward"
        "--movementDuration=6"
        "--relay2Offset=25"
        "--relaySpeed=5"
        "--requireOnward=1"
        "--onwardMaxAge=2"
      )

      {
        printf '%q' "$sim"
        printf ' %q' "${args[@]}"
        printf '\n'
      } > "$out/command.txt"

      (
        cd "$out"
        "$sim" "${args[@]}" > run.log 2>&1
      ) || {
        cat "$out/run.log"
        echo "Stopped at: $out"
        exit 1
      }

      if [[ $(grep -c '^SUMMARY ' "$out/run.log") -ne 1 ]]; then
        echo "Missing or duplicate SUMMARY: $out/run.log"
        exit 1
      fi

      printf '%s,%s,%s,%s,%s,%s,%s\n' \
        "$scenario" "$config" "$run" "$selector" "$rule" "$fast" \
        "$relative" >> "$batch/manifest.csv"

      completed=$((completed + 1))
      printf '[%d/150] %s %s run=%s: ' \
        "$completed" "$scenario" "$config" "$run"
      grep '^SUMMARY ' "$out/run.log"
    done
  done
done

printf '\nCompleted %d simulations.\nResults saved in: %s\n' \
  "$completed" "$batch"
