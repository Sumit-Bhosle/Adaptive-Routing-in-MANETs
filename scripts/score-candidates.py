"""Replay received signal observations and compare relay preferences."""

import argparse
import csv
import math
from pathlib import Path

# Provisional settings for this pilot experiment.
WINDOW = 3.0          # Seconds of recent observations used for the trend.
MAX_AGE = 2.0         # Older observations cannot support a candidate.
MIN_SPAN = 1.0        # Minimum time covered by trend observations.
MIN_SAMPLES = 3
LOW_DBM = -85.0       # Signal normalization bound, not a reception threshold.
HIGH_DBM = -50.0
RATE_SCALE = 5.0      # Weakening rate in dB/s mapped to maximum penalty.


def read_samples(path):
    samples = []
    previous_time = -1.0

    for number, line in enumerate(path.read_text().splitlines(), 1):
        if not line.startswith("SIGNAL_RX "):
            continue

        fields = dict(part.split("=", 1) for part in line.split()[1:])
        time = float(fields["t_s"])
        power = float(fields["signal_dbm"])

        if (not math.isfinite(time) or not math.isfinite(power)
                or time < previous_time):
            raise ValueError(
                f"Invalid or unordered signal sample at line {number}"
            )

        previous_time = time
        relay = int(fields["tx"])

        if fields["rx"] == "0" and relay in (1, 2):
            samples.append((time, relay, power))

    if not samples:
        raise ValueError("No signal samples from relays 1 and 2 at node 0")

    return samples


def measure(history, now):
    if not history:
        return {"status": "unseen"}

    last_time, power = history[-1]
    age = now - last_time
    result = {"age_s": age, "signal_dbm": power}

    recent = [(t, p) for t, p in history if t >= now - WINDOW]
    result["samples"] = len(recent)

    if age > MAX_AGE:
        return dict(result, status="stale")

    if (len(recent) < MIN_SAMPLES
            or recent[-1][0] - recent[0][0] < MIN_SPAN):
        return dict(result, status="warming_up")

    # Fit a straight-line trend through recent signal observations.
    mean_time = sum(t for t, p in recent) / len(recent)
    mean_power = sum(p for t, p in recent) / len(recent)
    numerator = sum(
        (t - mean_time) * (p - mean_power) for t, p in recent
    )
    denominator = sum((t - mean_time) ** 2 for t, p in recent)
    slope = numerator / denominator

    weakening = max(0.0, -slope)
    quality = min(
        1.0, max(0.0, (power - LOW_DBM) / (HIGH_DBM - LOW_DBM))
    )
    stability = 1.0 - min(weakening / RATE_SCALE, 1.0)

    return dict(
        result,
        status="eligible",
        weakening_db_s=weakening,
        quality=quality,
        stability=stability,
    )


def replay(samples, duration):
    history = {1: [], 2: []}
    cursor = 0
    rows = []
    previous_choices = None

    for now in range(1, duration + 1):
        # Only use observations already available at this decision time.
        while cursor < len(samples) and samples[cursor][0] <= now:
            time, relay, power = samples[cursor]
            history[relay].append((time, power))
            cursor += 1

        metrics = {
            relay: measure(history[relay], now) for relay in (1, 2)
        }
        eligible = {
            relay: values
            for relay, values in metrics.items()
            if values["status"] == "eligible"
        }

        max_rate = max(
            (values["weakening_db_s"] for values in eligible.values()),
            default=0.0,
        )

        # Apply the same adaptive weights to both candidates.
        stability_weight = 0.2 + 0.6 * min(max_rate / RATE_SCALE, 1.0)
        quality_weight = 1.0 - stability_weight

        for values in eligible.values():
            values["fixed_score"] = (
                0.5 * values["quality"] + 0.5 * values["stability"]
            )
            values["adaptive_score"] = (
                quality_weight * values["quality"]
                + stability_weight * values["stability"]
            )

        choices = []
        for field in ("signal_dbm", "fixed_score", "adaptive_score"):
            # All rules use the same eligibility checks.
            # Exact ties prefer the lower relay ID.
            winner = max(
                eligible,
                key=lambda relay: (eligible[relay][field], -relay),
                default=0,
            )
            choices.append(winner)

        if tuple(choices) != previous_choices:
            print(
                f"t={now}s strongest={choices[0]} "
                f"fixed={choices[1]} adaptive={choices[2]}"
            )
            previous_choices = tuple(choices)

        for relay, values in metrics.items():
            rows.append(dict(
                time_s=now,
                relay=relay,
                **values,
                quality_weight=quality_weight,
                stability_weight=stability_weight,
                strongest_choice=choices[0],
                fixed_choice=choices[1],
                adaptive_choice=choices[2],
            ))

    return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path)
    parser.add_argument("--duration", type=int, default=30)
    args = parser.parse_args()

    if args.duration < 1:
        parser.error("--duration must be a positive integer")

    rows = replay(read_samples(args.log), args.duration)
    output = args.log.parent / "candidate-scores.csv"

    fields = [
        "time_s", "relay", "status", "age_s", "signal_dbm", "samples",
        "weakening_db_s", "quality", "stability", "quality_weight",
        "stability_weight", "fixed_score", "adaptive_score",
        "strongest_choice", "fixed_choice", "adaptive_choice",
    ]

    with output.open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields, lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)

    print(f"Saved {output}")
    print("Choice 0 means no eligible candidate.")


if __name__ == "__main__":
    main()
