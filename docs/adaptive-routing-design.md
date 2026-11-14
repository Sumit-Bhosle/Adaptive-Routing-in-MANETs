# Mobility-aware route selection

## Research question

When two relays offer valid two-hop routes to the same destination,
can recent radio observations help the source choose a route that stays usable longer?

## First experiment

Use four nodes: a source, a destination, and two candidate relays.
Keep one relay stationary and move the other gradually. Run standard AODV
and the proposed method with the same movement and packet schedule.

## Proposed decision rule

1. Record the time and received signal strength of recent messages from each relay.
2. Estimate current link quality from the latest observations.
3. Estimate stability from how quickly the signal has been weakening.
4. When recent signal changes are small, give current quality more weight.
   When sustained changes are observed, give stability more weight.
5. Among eligible routes of equal freshness and hop count, prefer the relay
   with the higher score. Record the inputs, weights, and selected relay.

Score = (quality weight × quality) + (stability weight × stability)

Both inputs must use a common scale from 0 to 1. The weights must sum to 1.
The first prototype evaluates the source-to-relay link. The experiment
must also verify that each candidate relay can reach the destination.

## First validation

Before changing AODV, record signal observations while one relay moves.
Check whether the trace can distinguish the weakening link from the steady
one before either route breaks.

## Open implementation decisions

Use the observed traces to define normalization, the observation window,
and the threshold for a sustained change. Specify how a node handles
missing observations and how it obtains two eligible route candidates.
Count any added beacon traffic in the routing overhead.
