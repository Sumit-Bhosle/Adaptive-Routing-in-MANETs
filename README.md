# Adaptive Routing in MANETs

A lightweight, mobility-aware adaptive routing approach for Mobile Ad Hoc Networks (MANETs), to be investigated through NS-3 simulation.

**Course:** COMP 5213 – Computer Networks  
**Group:** Group 8  
**Status:** Topic approved; implementation in progress

## 1. Overview

Mobile Ad Hoc Networks consist of wireless devices that communicate without requiring fixed network infrastructure. Devices may also forward packets for other devices.

Because nodes can move, wireless links and network routes can change frequently. This can lead to route failures, packet loss, and increased communication delay.

This project investigates whether a lightweight, adaptive route-selection mechanism can improve MANET performance under different mobility conditions.

## 2. Problem Statement

Standard routing protocols such as AODV discover routes when needed, but changing network conditions can cause established routes to fail.

Our project will investigate a route-selection mechanism that adjusts the importance of selected routing factors according to observed mobility conditions.

## 3. Objectives

- Establish a working MANET simulation using NS-3.
- Evaluate standard AODV as a baseline.
- Design and implement a lightweight adaptive route-selection mechanism.
- Investigate how mobility-dependent routing decisions affect network performance.
- Compare the proposed approach against the baseline under controlled simulation conditions.
- Analyze and document experimental results.

## 4. Proposed Approach

The initial design will investigate the use of factors such as link quality, link stability, and mobility.

The proposed workflow is:

1. Observe relevant network conditions.
2. Estimate the current mobility regime.
3. Adjust the importance of selected routing factors.
4. Evaluate candidate routing decisions using the resulting scores.
5. Measure performance under different mobility scenarios.

The precise scoring formula, mobility thresholds, and integration with AODV will be finalized during implementation and experimentation.

## 5. Experimental Evaluation

The baseline and proposed approach will be evaluated under comparable simulation conditions.

Planned metrics:

- Packet Delivery Ratio (PDR)
- Throughput
- End-to-End Delay
- Routing Overhead
- Route Breaks

The experiments will investigate whether the proposed mechanism improves performance and identify any trade-offs.

## 6. Technology Stack

- **Operating system:** Ubuntu running in a virtual machine
- **Network simulation:** NS-3
- **Implementation:** C++
- **Analysis and visualization:** Python, pandas, NumPy, and Matplotlib
- **Version control:** Git and GitHub

## 7. Research Foundation

The initial literature review is guided by the following journal papers:

1. *An Adaptive Reinforcement Learning-Based Mobility-Aware Routing for Heterogeneous Wireless Body Area Networks* (2024).
2. *Topology Control Using Optimized Clustering Scheme in Mobile Ad Hoc Network* (2024).
3. *An Adaptive Congestion and Energy Aware Multipath Routing Scheme for Mobile Ad-Hoc Networks Through Stable Link Prediction* (2023).

These papers provide background on mobility-aware routing, topology control, link stability, and adaptive routing decisions. They will inform our design without being reproduced as complete implementations.

## 8. Project Status

- [x] GitHub repository created.
- [x] Project topic approved.
- [ ] Ubuntu development environment verified.
- [ ] NS-3 installed and tested.
- [ ] AODV baseline simulation implemented.
- [ ] Adaptive mechanism implemented.
- [ ] Experiments completed and results analyzed.

## 9. Repository

Source code, documentation, and reproducible experiment configurations will be maintained in this repository as development progresses.
