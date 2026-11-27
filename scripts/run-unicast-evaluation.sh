#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
sim="$HOME/Projects/ns-3.46/build/scratch/ns3.46-relay-unicast-validation-debug"

if [[ ! -x "$sim" ]]; then
  printf 'Executable not found: %s\n' "$sim" >&2
  exit 1
fi

mkdir -p "$project_dir/results/relay-unicast-validation"
batch_dir=$(mktemp -d \
  "$project_dir/results/relay-unicast-validation/repeated-XXXXXX")

# Preserve the exact source used for this batch.
cp "$project_dir/simulations/relay-unicast-validation.cc" \
  "$batch_dir/relay-unicast-validation-source.cc"

printf 'scenario,method,run,relative_directory\n' \
  > "$batch_dir/manifest.csv"

printf 'Results directory: %s\n' "$batch_dir"
completed=0

for scenario in stationary away toward; do
  case "$scenario" in
    stationary) move=0; toward=0 ;;
    away)       move=1; toward=0 ;;
    toward)     move=1; toward=1 ;;
  esac

  for method in aodv strongest fixed stars; do
    case "$method" in
      aodv)     enabled=0; rule=2 ;;
      strongest) enabled=1; rule=0 ;;
      fixed)     enabled=1; rule=1 ;;
      stars)     enabled=1; rule=2 ;;
    esac

    for run in {1..10}; do
      relative="$scenario/$method/run-$run"
      out="$batch_dir/$relative"
      mkdir -p "$out"

      if ! (
        cd "$out"
        "$sim" \
          --rngRun="$run" \
          --adaptiveForwarding="$enabled" \
          --selectionRule="$rule" \
          --moveRelay="$move" \
          --relay2Offset=25 \
          --relaySpeed=5 \
          --towardSource="$toward" \
          --movementDuration=6 \
          --requireOnward=1 \
          --onwardMaxAge=2 > run.log 2>&1
      ); then
        printf '\nFAILED: %s\n' "$out" >&2
        cat "$out/run.log"
        exit 1
      fi

      if [[ $(grep -c '^SUMMARY ' "$out/run.log") -ne 1 ]]; then
        printf 'Missing or duplicate SUMMARY: %s\n' "$out" >&2
        exit 1
      fi

      printf '%s,%s,%s,%s\n' \
        "$scenario" "$method" "$run" "$relative" \
        >> "$batch_dir/manifest.csv"

      completed=$((completed + 1))
      printf '[%3d/120] %s / %s / run %s: ' \
        "$completed" "$scenario" "$method" "$run"
      grep '^SUMMARY ' "$out/run.log"
    done
  done
done

printf '\nCompleted %s simulations.\n' "$completed"
printf 'Results saved in: %s\n' "$batch_dir"
