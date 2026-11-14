# Adaptive Routing in MANETs — evaluation and presentation plan
COMP 5213 · Group 8 · Draft for discussion · November 11, 2026

## 1. Research question

When movement weakens a wireless link, can a lightweight rule use recent signal observations to choose a more stable route, improve delivery, and justify the extra routing traffic it generates?

## 2. Rules for claiming completion

1. Explain the proposed rule in plain language, an equation, and pseudocode. Identify the decision that differs from standard AODV.
2. Trace one real choice from observed signals through candidate scores to the installed next hop. Use only information available to the node at decision time.
3. Run standard AODV and the proposed implementation under identical radio settings, movement traces, traffic, and observation windows. Count all signaling needed by each method.
4. State the settings that produced each result. Vary one main factor at a time. Use multiple independent runs for the main comparisons and match each run across methods.
5. Calculate results from saved simulation output. Keep an example topology, packet trace and routing-table snapshot alongside aggregate curves.
6. For every figure, provide axis names, units, legend, scenario, number of runs, and the meaning of error bars. Label single-run timelines as examples. Explain the trade-off or limitation visible in each figure.
7. Keep a reproducible path from C++ scenario to per-run CSV to Gnuplot figure. Cite each paper for the idea used. Report adverse results as well as improvements.

## 3. Evidence already obtained

| Standard AODV demonstration | Observation | Interpretation |
|---|---|---|
| Three nodes, one relay | Stationary 30/30. Relay removed at 10 s: 10/30. | The only usable path disappeared. |
| Four nodes, two relays | Stationary 30/30. Active relay removed: 29/30. Gateway changed from 10.0.0.2 at 8 s to 10.0.0.3 at 12 s. | Standard AODV recovered through the alternate relay. |
| Four-node receiver trace | Packet 10 missing. Packet 11 arrived at 11.507394 s. | First observed delivery was 1.507394 s after movement. Exact route installation time was not recorded. |

These are baseline demonstrations. Their radio models differ, so report each stationary/moving pair together. Use one common model for all final protocol comparisons.

## 4. Literature foundation

| Paper | Problem and method | How it informs this project |
|---|---|---|
| Arafat, Pan & Bak (2024), An Adaptive Reinforcement Learning-Based Mobility-Aware Routing for Heterogeneous Wireless Body Area Networks | In a vehicular healthcare setting, ARMR estimates link lifetime and uses adaptive Q-learning to guide forwarding. | A link that works now may soon fail. Test whether recent observations can support a simpler stability estimate. |
| Agrawal & Rathi (2024), Topology Control Using Optimized Clustering Scheme in Mobile Ad Hoc Network | An improved rabbit optimization algorithm selects cluster heads using multiple criteria; cluster maintenance handles topology changes. The authors evaluate throughput and energy efficiency. | Motivates varying network size and movement. Its cluster-head decision is distinct from our next-hop decision. |
| Arun & Jayanthi (2023), An Adaptive Congestion and Energy Aware Multipath Routing Scheme for Mobile Ad-Hoc Networks Through Stable Link Prediction | ACEAMR combines link prediction, queue utilization and residual energy to identify candidate paths. | Motivates comparing viable alternatives and measuring the cost of richer decisions. Energy can be evaluated separately from the route score. |

Use a literature slide with columns for problem, method, finding and idea adopted. Published performance gains belong to each paper’s experimental setting.

## 5. Method and implementation order

1. Step 4B: move one relay gradually with a distance-dependent propagation model. Log time, transmitter identity and received signal strength for each received probe. Compare against a stationary relay. Record missing probes. The old hard cutoff was suitable for demonstrating failure.
2. On pilot traces, define quality Q and stability S on a common 0–1 scale. Define a recent-sample window, handling for lost or stale samples, and a reproducible trigger for changing weights. Candidate score = wQ × Q + (1 − wQ) × S. Lower wQ when sustained weakening calls for greater stability weight. Freeze the rule before the evaluation runs.
3. Define where the protocol sees alternative valid candidates, what information they carry, and how long it waits. Stock AODV can discard duplicate route requests. A score alone cannot make a second route available. Keep route freshness and loop-prevention constraints. Specify fallback and tie-breaking. A first-hop signal describes that link; validate the onward path before generalizing to a whole route.
4. Demonstrate a real choice between two valid routes. Log both scores and the gateway actually installed. Include a stable case where an unnecessary switch would be undesirable.
5. Compare standard AODV with the adaptive method. Add the same modified implementation with fixed weights to isolate the value of changing weights. Give the two modified versions identical candidate discovery and probing. Add a second stock protocol such as OLSR if resources permit or the instructor expects multiple standard baselines.

## 6. Experiment settings and figures

Proposed pilot starting points, subject to connectivity and VM runtime. Record the ns-3 version, area, Wi-Fi standard, channel width, data rate, transmit power, loss model, mobility model and pause time, speed, nodes, traffic pairs, packet size, offered load, duration, warm-up, measurement window, drain time, seeds and repetitions. Distinguish channel width (MHz), radio data rate (Mb/s), and application offered load (kb/s).

| Experiment | Change | Hold constant | Main Gnuplot output |
|---|---|---|---|
| Four-node trace | Time and relay motion | Geometry, traffic and radio model across methods | Signal vs time by relay; route changes and packet arrivals vs time |
| Mobility sweep | Example speed settings 0, 1, 3, 5, 10 m/s | Node count, area, traffic, duration and radio | Delivery, throughput, delay and overhead vs speed |
| Network-size sweep | Example 20, 40, 60 nodes | Area, speed, traffic pairs and total offered load | Throughput, loss and overhead vs node count; interpret density changes |
| Traffic-load sweep | Example 1, 5, 10 packets/s per flow | Packet size, radio rate, nodes and movement | Goodput and delay vs offered load |
| Radio-rate sensitivity | Supported fixed PHY rates, if feasible | Channel width, offered load and mobility | Delivery or goodput vs configured radio rate |

Start with a 120 s pilot and five paired runs per setting as a workload estimate. Select a common measurement window after checking initialization and outstanding packets. Save mobility traces or assign explicit random streams so the same seed truly produces the same movement across methods. Extend repetitions if uncertainty is large. Keep the original 30 s demos as examples.

## 7. Metric definitions

| Metric | Calculation and reporting convention |
|---|---|
| Delivery / loss (%) | Unique application packets received / sent × 100. Loss = 100 − delivery after the defined drain period. Use one metric per main figure when both tell the same story. |
| Application throughput (goodput, kb/s) | 8 × delivered application payload bytes / measurement seconds / 1000. State which headers are excluded. Use a fixed time window across runs. |
| End-to-end delay (ms) | Receive time − send time for each delivered packet. Report mean and optionally 95th percentile. Pair with loss because missing packets contribute no delay samples. |
| Routing overhead | Count control transmissions per hop, including forwarded control packets and new probes. Report control bytes/s and optionally control transmissions per delivered data packet. State the counted protocol layer. |
| Route breaks and recovery | Count logged active-route failures per run. Report time to first successful delivery after a known break. Track protocol repair time separately if the routing implementation logs it. |
| Energy consumed (J); J per delivered packet | If a radio energy model is configured, sum initial minus remaining energy over nodes. Document voltage, state currents and run duration. Leave J/packet undefined when zero packets arrive. |

Use one observation per independent run for summary statistics. Plot per-method means and 95% intervals across runs. For claims about improvement, calculate paired differences on matched runs. Preserve cases with zero deliveries; delay is undefined there.

## 8. Report and final presentation

1. Introduce one packet’s journey and the research question.
2. Summarize the three papers and identify the design choice they motivated.
3. Explain standard AODV and the custom rule with one numerical example.
4. Show the ns-3 implementation, topology, configuration table and fairness controls.
5. Show the verified baseline snapshots and packet timeline.
6. Show a measured custom routing decision, then comparative Gnuplot curves.
7. Discuss costs, uncertainty, failure cases and when the adaptive method helps.
8. Conclude against the research question. Link the implementation, raw results, CSVs and plot scripts.

For each main plot retain the .gp script, its input CSV and exported SVG or PNG. The report can include the full metric matrix and supplemental figures; the presentation should prioritize the curves that explain the routing decision and its consequences.
