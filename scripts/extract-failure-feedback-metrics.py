from pathlib import Path
from statistics import mean
from collections import defaultdict
import csv
import math
import sys

root = Path(sys.argv[1])
scenarios = ("stationary", "away", "toward")
methods = ("aodv", "strongest", "fixed", "stars-original", "stars-fast")

DURATION = 30
APP_BYTES = 128       # UDP payload, including the application SeqTsHeader
MONITOR_BYTES = 64    # UDP payload for each monitoring message
rows = []
time_rows = []

def fields(line):
    return dict(token.split("=", 1) for token in line.split()[1:]
                if "=" in token)

def average(values):
    return mean(values) if values else None

def write_csv(path, records):
    with path.open("w", newline="") as handle:
        writer = csv.DictWriter(
            handle, fieldnames=list(records[0]), lineterminator="\n"
        )
        writer.writeheader()
        writer.writerows(records)

for scenario in scenarios:
    for method in methods:
        for run in range(1, 11):
            log = root / scenario / method / f"run-{run}" / "run.log"
            events = defaultdict(list)

            for line in log.read_text().splitlines():
                if line.strip():
                    events[line.split()[0]].append(fields(line))

            if len(events["SUMMARY"]) != 1:
                raise SystemExit(f"Expected exactly one SUMMARY: {log}")

            summary = events["SUMMARY"][0]
            sent = int(summary["sent"])
            received = int(summary["received"])
            if sent != 30 or not 0 <= received <= sent:
                raise SystemExit(f"Unexpected packet counts: {log}")

            arrivals = {}
            for event in events["UDP_RX"]:
                seq = int(event["seq"])
                arrival = float(event["t_s"])
                delay = float(event["delay_ms"])

                if seq in arrivals:
                    raise SystemExit(f"Duplicate application sequence {seq}: {log}")
                if not (
                    0 <= seq < sent
                    and math.isfinite(arrival)
                    and math.isfinite(delay)
                    and 0 <= arrival < DURATION
                    and delay >= 0
                ):
                    raise SystemExit(f"Invalid arrival record: {log}")

                # Known application schedule: first send 0.5 s, interval 1 s.
                send_time = 0.5 + seq
                if abs((arrival - send_time) * 1000 - delay) > 0.01:
                    raise SystemExit(f"Delay/schedule mismatch, seq={seq}: {log}")
                arrivals[seq] = (arrival, delay, send_time)

            if len(arrivals) != received:
                raise SystemExit(f"Arrival count differs from SUMMARY: {log}")

            def relay_summaries(name):
                entries = events[name]
                if len(entries) != 2 or {
                    int(e["relay"]) for e in entries
                } != {1, 2}:
                    raise SystemExit(f"Missing/duplicate {name}: {log}")
                return entries

            probe = relay_summaries("PROBE_SUMMARY")
            validation = relay_summaries("UNICAST_TEST_SUMMARY")
            feedback = relay_summaries("FEEDBACK_SUMMARY")
            failure = relay_summaries("FAILURE_SUMMARY")

            def counts(entries, attempt_key, accepted_key):
                attempted = sum(int(e[attempt_key]) for e in entries)
                accepted = sum(int(e[accepted_key]) for e in entries)
                if not 0 <= accepted <= attempted:
                    raise SystemExit(f"Invalid monitoring counters: {log}")
                return attempted, accepted

            components = {
                "neighbour_probe": counts(probe, "attempted", "socket_accepted"),
                "unicast_validation": counts(
                    validation, "attempted", "socket_accepted"
                ),
                "destination_confirmation": counts(
                    feedback, "destination_attempted",
                    "destination_socket_accepted"
                ),
                "onward_report": counts(
                    feedback, "relay_report_attempted",
                    "relay_report_socket_accepted"
                ),
                "failure_report": counts(
                    failure, "report_attempted", "socket_accepted"
                ),
            }

            delays = [v[1] for v in arrivals.values()]
            pre = [v[1] for v in arrivals.values() if v[2] < 10]
            post = [v[1] for v in arrivals.values() if v[2] >= 10]
            times = sorted(v[0] for v in arrivals.values())
            gaps = [b - a for a, b in zip(times, times[1:])]

            row = {
                "scenario": scenario,
                "method": method,
                "run": run,
                "sent_packets": sent,
                "received_packets": received,
                "missing_sequences": ";".join(
                    str(s) for s in range(sent) if s not in arrivals
                ),
                "pdr_percent": 100 * received / sent,
                "udp_payload_goodput_kbit_s": received * APP_BYTES * 8 / 30000,
                "delay_all_received_mean_ms": average(delays),
                "delay_sent_before_10s_mean_ms": average(pre),
                "delay_sent_from_10s_mean_ms": average(post),
                "delay_received_max_ms": max(delays) if delays else None,
                "sent_from_10s_received_packets": len(post),
                "sent_from_10s_pdr_percent": 100 * len(post) / 20,
                "largest_observed_arrival_gap_s": max(gaps) if gaps else None,
            }

            for name, (attempted, accepted) in components.items():
                row[f"{name}_attempted_packets"] = attempted
                row[f"{name}_socket_accepted_packets"] = accepted

            row["monitor_attempted_payload_bytes"] = (
                sum(v[0] for v in components.values()) * MONITOR_BYTES
            )
            row["monitor_socket_accepted_payload_bytes"] = (
                sum(v[1] for v in components.values()) * MONITOR_BYTES
            )
            row["failure_reports_received_packets"] = sum(
                int(e["received_at_source"]) for e in failure
            )
            rows.append(row)

            for second in range(DURATION):
                count = sum(
                    second <= v[0] < second + 1 for v in arrivals.values()
                )
                time_rows.append({
                    "scenario": scenario,
                    "method": method,
                    "run": run,
                    "bin_start_s": second,
                    "bin_end_s": second + 1,
                    "received_packets": count,
                    "udp_payload_goodput_kbit_s": count * APP_BYTES * 8 / 1000,
                })

# Calculate averages of per-run metrics, retaining min/max across runs.
metric_names = (
    "pdr_percent",
    "udp_payload_goodput_kbit_s",
    "delay_all_received_mean_ms",
    "delay_sent_before_10s_mean_ms",
    "delay_sent_from_10s_mean_ms",
    "sent_from_10s_pdr_percent",
    "monitor_socket_accepted_payload_bytes",
    "failure_report_socket_accepted_packets",
)

summary_rows = []
for scenario in scenarios:
    for method in methods:
        selected = [
            r for r in rows
            if r["scenario"] == scenario and r["method"] == method
        ]
        record = {"scenario": scenario, "method": method, "runs": len(selected)}
        for metric in metric_names:
            values = [r[metric] for r in selected if r[metric] is not None]
            record[f"{metric}_valid_runs"] = len(values)
            record[f"{metric}_mean"] = average(values)
            record[f"{metric}_min"] = min(values) if values else None
            record[f"{metric}_max"] = max(values) if values else None
        summary_rows.append(record)

groups = defaultdict(list)
for row in time_rows:
    groups[row["scenario"], row["method"], row["bin_start_s"]].append(
        row["udp_payload_goodput_kbit_s"]
    )

time_summary = []
for (scenario, method, second), values in groups.items():
    time_summary.append({
        "scenario": scenario,
        "method": method,
        "bin_start_s": second,
        "bin_end_s": second + 1,
        "runs": len(values),
        "mean_kbit_s": mean(values),
        "min_kbit_s": min(values),
        "max_kbit_s": max(values),
    })

out = root / "metrics"
out.mkdir(exist_ok=True)
write_csv(out / "per-run.csv", rows)
write_csv(out / "summary.csv", summary_rows)
write_csv(out / "goodput-per-second.csv", time_rows)
write_csv(out / "goodput-per-second-summary.csv", time_summary)

(out / "definitions.txt").write_text(
    "Application UDP payload: 128 bytes, including SeqTsHeader.\n"
    "Whole-run goodput denominator: 30 seconds; decimal kbit/s.\n"
    "Time-series bins: reception times in [start, end), width 1 second.\n"
    "Delay includes received packets only; losses have no delay value.\n"
    "Before/from 10 s groups use packet SEND time, not arrival time.\n"
    "For stationary runs, 10 s is a matched analysis boundary only.\n"
    "Each run sends 10 packets before 10 s and 20 from 10 s onward.\n"
    "Group delay results average per-run means; valid-run counts are saved.\n"
    "Min/max describe observed run ranges, not confidence intervals.\n"
    "Monitoring counters cover neighbour probes, unicast validation,\n"
    "destination confirmations, onward reports, and failure reports.\n"
    "Monitoring payload is 64 bytes per socket-accepted message.\n"
    "Socket acceptance does not prove transmission or delivery.\n"
    "These counters EXCLUDE IP/UDP/MAC headers, AODV, ARP, ACKs and retries.\n"
    "Largest observed arrival gap is not an exact route-repair time.\n"
)

print("Verified all 150 logs against application reception records.\n")
print(
    f"{'Scenario':10} {'Method':15} {'PDR %':>8} {'kbit/s':>9} "
    f"{'Delay >=10s ms':>15} {'Monitor bytes':>14} {'Failure pkts':>13}"
)

for r in summary_rows:
    delay = r["delay_sent_from_10s_mean_ms_mean"]
    delay_text = f"{delay:.4f}" if delay is not None else "N/A"
    print(
        f"{r['scenario']:10} {r['method']:15} "
        f"{r['pdr_percent_mean']:8.3f} "
        f"{r['udp_payload_goodput_kbit_s_mean']:9.4f} "
        f"{delay_text:>15} "
        f"{r['monitor_socket_accepted_payload_bytes_mean']:14.1f} "
        f"{r['failure_report_socket_accepted_packets_mean']:13.1f}"
    )

print(f"\nSaved CSV files and metric definitions in: {out}")
