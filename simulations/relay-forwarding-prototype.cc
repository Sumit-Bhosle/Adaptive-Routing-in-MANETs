// SPDX-License-Identifier: GPL-2.0-only
#include "ns3/aodv-module.h"
#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/wifi-module.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <limits>
#include <string>
#include <vector>
#include <iomanip>
#include <iostream>

using namespace ns3;

// Controlled prototype: relay onward routes are configured in advance.
static bool adaptiveForwarding = false;
static Ptr<Ipv4StaticRouting> sourceRoutes;
static Ipv4Address destinationAddress;
static std::array<Ipv4Address, 3> relayAddresses;
static uint32_t sourceInterface = 0;
static uint32_t installedRelay = 0;

static void
ApplyRelayChoice(uint32_t relayId)
{
    if (!adaptiveForwarding || relayId == installedRelay)
    {
        return;
    }

    // Remove only our destination host route.
    for (uint32_t i = sourceRoutes->GetNRoutes(); i > 0; --i)
    {
        const auto route = sourceRoutes->GetRoute(i - 1);
        if (route.IsHost() && route.GetDest() == destinationAddress)
        {
            sourceRoutes->RemoveRoute(i - 1);
        }
    }

    const uint32_t previous = installedRelay;
    if (relayId != 0)
    {
        sourceRoutes->AddHostRouteTo(
            destinationAddress, relayAddresses.at(relayId), sourceInterface);
    }
    installedRelay = relayId;

    std::cout << std::fixed << std::setprecision(6)
              << "ROUTE_SELECTION t_s=" << Simulator::Now().GetSeconds()
              << " previous=" << previous
              << " selected=" << relayId
              << " mode=" << (relayId ? "adaptive" : "AODV-fallback")
              << '\n';
}

// An IP forwarding event is an attempt; destination reception confirms delivery.
static void
RecordForward(uint32_t nodeId,
              const Ipv4Header& header,
              Ptr<const Packet> packet,
              uint32_t interface)
{
    if (header.GetProtocol() != 17 ||
        header.GetDestination() != destinationAddress ||
        header.GetFragmentOffset() != 0)
    {
        return;
    }

    auto copy = packet->Copy();
    UdpHeader udp;
    if (copy->RemoveHeader(udp) == 0 || udp.GetDestinationPort() != 9000)
    {
        return;
    }

    SeqTsHeader sequence;
    if (copy->GetSize() < sequence.GetSerializedSize() ||
        copy->PeekHeader(sequence) == 0)
    {
        return;
    }

    std::cout << std::fixed << std::setprecision(6)
              << "UDP_FORWARD t_s=" << Simulator::Now().GetSeconds()
              << " node=" << nodeId
              << " seq=" << sequence.GetSeq()
              << " interface=" << interface
              << " ttl=" << static_cast<uint32_t>(header.GetTtl())
              << '\n';
}


// Pilot scoring settings: identical to scripts/score-candidates.py.
constexpr double SCORE_WINDOW = 3.0;
constexpr double SCORE_MAX_AGE = 2.0;
constexpr double SCORE_MIN_SPAN = 1.0;
constexpr std::size_t SCORE_MIN_SAMPLES = 3;
constexpr double SCORE_LOW_DBM = -85.0;
constexpr double SCORE_HIGH_DBM = -50.0;
constexpr double SCORE_RATE_SCALE = 5.0;

struct SignalSample
{
    double time;
    double power;
};

struct CandidateMetrics
{
    std::string status = "unseen";
    double age = std::numeric_limits<double>::quiet_NaN();
    double power = std::numeric_limits<double>::quiet_NaN();
    std::size_t samples = 0;
    double weakening = std::numeric_limits<double>::quiet_NaN();
    double quality = std::numeric_limits<double>::quiet_NaN();
    double stability = std::numeric_limits<double>::quiet_NaN();
    double fixedScore = std::numeric_limits<double>::quiet_NaN();
    double adaptiveScore = std::numeric_limits<double>::quiet_NaN();
};

// Relay IDs are 1 and 2; index 0 is unused.
static std::array<std::vector<SignalSample>, 3> signalHistory;
static std::ofstream scoreFile;

static CandidateMetrics
MeasureCandidate(uint32_t relayId, double now)
{
    CandidateMetrics result;
    const auto& history = signalHistory.at(relayId);

    if (history.empty())
    {
        return result;
    }

    result.age = now - history.back().time;
    result.power = history.back().power;

    std::vector<SignalSample> recent;
    for (const auto& sample : history)
    {
        if (sample.time >= now - SCORE_WINDOW)
        {
            recent.push_back(sample);
        }
    }
    result.samples = recent.size();

    if (result.age > SCORE_MAX_AGE)
    {
        result.status = "stale";
        return result;
    }

    if (recent.size() < SCORE_MIN_SAMPLES ||
        recent.back().time - recent.front().time < SCORE_MIN_SPAN)
    {
        result.status = "warming_up";
        return result;
    }

    double meanTime = 0.0;
    double meanPower = 0.0;
    for (const auto& sample : recent)
    {
        meanTime += sample.time;
        meanPower += sample.power;
    }
    meanTime /= recent.size();
    meanPower /= recent.size();

    double numerator = 0.0;
    double denominator = 0.0;
    for (const auto& sample : recent)
    {
        const double dt = sample.time - meanTime;
        numerator += dt * (sample.power - meanPower);
        denominator += dt * dt;
    }

    NS_ABORT_MSG_IF(denominator <= 0.0, "Invalid signal history span");

    const double slope = numerator / denominator;
    result.weakening = std::max(0.0, -slope);
    result.quality = std::clamp(
        (result.power - SCORE_LOW_DBM) /
            (SCORE_HIGH_DBM - SCORE_LOW_DBM),
        0.0, 1.0);
    result.stability =
        1.0 - std::min(result.weakening / SCORE_RATE_SCALE, 1.0);
    result.status = "eligible";
    return result;
}

// Missing measurements are written as empty CSV fields.
static void
WriteNumber(double value)
{
    if (std::isfinite(value))
    {
        scoreFile << value;
    }
}

static void
EvaluateCandidates()
{
    const double now = Simulator::Now().GetSeconds();
    std::array<CandidateMetrics, 3> metrics;
    double maxRate = 0.0;

    for (uint32_t relayId : {1u, 2u})
    {
        metrics[relayId] = MeasureCandidate(relayId, now);
        if (metrics[relayId].status == "eligible")
        {
            maxRate = std::max(maxRate, metrics[relayId].weakening);
        }
    }

    const double stabilityWeight =
        0.2 + 0.6 * std::min(maxRate / SCORE_RATE_SCALE, 1.0);
    const double qualityWeight = 1.0 - stabilityWeight;

    std::array<uint32_t, 3> choices{0, 0, 0};
    std::array<double, 3> best{
        -std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity()
    };

    for (uint32_t relayId : {1u, 2u})
    {
        auto& candidate = metrics[relayId];
        if (candidate.status != "eligible")
        {
            continue;
        }

        candidate.fixedScore =
            0.5 * candidate.quality + 0.5 * candidate.stability;
        candidate.adaptiveScore =
            qualityWeight * candidate.quality +
            stabilityWeight * candidate.stability;

        const std::array<double, 3> values{
            candidate.power,
            candidate.fixedScore,
            candidate.adaptiveScore
        };

        // Ascending IDs and strict comparison preserve lower-ID exact ties.
        for (std::size_t rule = 0; rule < values.size(); ++rule)
        {
            if (values[rule] > best[rule])
            {
                best[rule] = values[rule];
                choices[rule] = relayId;
            }
        }
    }

    for (uint32_t relayId : {1u, 2u})
    {
        const auto& candidate = metrics[relayId];
        scoreFile << now << ',' << relayId << ',' << candidate.status << ',';
        WriteNumber(candidate.age);
        scoreFile << ',';
        WriteNumber(candidate.power);
        scoreFile << ',';
        if (candidate.status != "unseen")
        {
            scoreFile << candidate.samples;
        }
        scoreFile << ',';
        WriteNumber(candidate.weakening);
        scoreFile << ',';
        WriteNumber(candidate.quality);
        scoreFile << ',';
        WriteNumber(candidate.stability);
        scoreFile << ',' << qualityWeight << ',' << stabilityWeight << ',';
        WriteNumber(candidate.fixedScore);
        scoreFile << ',';
        WriteNumber(candidate.adaptiveScore);
        scoreFile << ',' << choices[0] << ',' << choices[1]
                  << ',' << choices[2] << '\n';
    }

    std::cout << std::fixed << std::setprecision(6)
              << "LIVE_CHOICE t_s=" << now
              << " strongest=" << choices[0]
              << " fixed=" << choices[1]
              << " adaptive=" << choices[2]
              << " stability_weight=" << stabilityWeight
              << " forwarding="
              << (adaptiveForwarding ? "adaptive-prototype" : "AODV")
              << '\n';

    ApplyRelayChoice(choices[2]);
}


// Record application delivery time, sequence number, and delay.
static void
RecordArrival(Ptr<const Packet> packet)
{
    SeqTsHeader header;
    if (packet->PeekHeader(header) == 0)
    {
        return;
    }

    std::cout << std::fixed << std::setprecision(6)
              << "UDP_RX"
              << " t_s=" << Simulator::Now().GetSeconds()
              << " seq=" << header.GetSeq()
              << " delay_ms="
              << (Simulator::Now() - header.GetTs()).GetSeconds() * 1000.0
              << '\n';
}


// Observe successfully received Wi-Fi data frames from a selected relay.
// These may carry application traffic, routing messages, or other data.
// They are not necessarily addressed to the observing node.
static void
RecordLinkSignal(uint32_t observerId,
                 uint32_t relayId,
                 Mac48Address relayMac,
                 Ptr<const Packet> packet,
                 uint16_t,
                 WifiTxVector,
                 MpduInfo,
                 SignalNoiseDbm signalNoise,
                 uint16_t)
{
    WifiMacHeader header;
    if (packet->PeekHeader(header) == 0 || !header.IsData())
    {
        return;
    }
    if (header.GetAddr2() != relayMac)
    {
        return;
    }

    if (observerId == 0)
    {
        const double now = Simulator::Now().GetSeconds();
        signalHistory.at(relayId).push_back({now, signalNoise.signal});

        // Preserve precision so Python can replay the same scoring inputs.
        std::cout << std::defaultfloat << std::setprecision(17)
                  << "SIGNAL_RX t_s=" << now
                  << " tx=" << relayId
                  << " rx=0"
                  << " signal_dbm=" << signalNoise.signal << '\n';
    }

    std::cout << std::fixed << std::setprecision(6)
              << "LINK_SIGNAL"
              << " t_s=" << Simulator::Now().GetSeconds()
              << " relay=" << relayId
              << " observer=" << observerId
              << " signal_dbm=" << signalNoise.signal
              << " noise_dbm=" << signalNoise.noise
              << " snr_db=" << signalNoise.signal - signalNoise.noise
              << " mac_seq=" << header.GetSequenceNumber()
              << '\n';
}

int
main(int argc, char* argv[])
{
    uint32_t activeRelay = 0;
    uint32_t moveRelay = 0;

    CommandLine cmd(__FILE__);
    cmd.AddValue("activeRelay",
                 "0=both, 1=relay 1 only, 2=relay 2 only, 3=neither",
                 activeRelay);
    cmd.AddValue("moveRelay",
                 "0=stationary; 1=move relay 1 upward from 10 s",
                 moveRelay);
    cmd.AddValue("adaptiveForwarding",
                 "Enable adaptive source selection with configured onward routes",
                 adaptiveForwarding);
    cmd.Parse(argc, argv);
    NS_ABORT_MSG_IF(moveRelay > 1, "Use --moveRelay=0 or 1");
    NS_ABORT_MSG_IF(activeRelay != 0,
                    "This experiment requires both relays: --activeRelay=0");
    NS_ABORT_MSG_IF(activeRelay > 3, "Use --activeRelay=0, 1, 2, or 3");

    RngSeedManager::SetSeed(12345);
    RngSeedManager::SetRun(1);

    const bool relay1Nearby = activeRelay == 0 || activeRelay == 1;
    const bool relay2Nearby = activeRelay == 0 || activeRelay == 2;

    NodeContainer nodes;
    nodes.Create(4);

    auto positions = CreateObject<ListPositionAllocator>();
    positions->Add(Vector(0, 0, 0));
    positions->Add(relay1Nearby ? Vector(35, 10, 0)
                               : Vector(10000, 10000, 0));
    positions->Add(relay2Nearby ? Vector(35, -10, 0)
                               : Vector(-10000, -10000, 0));
    positions->Add(Vector(70, 0, 0));

    MobilityHelper mobility;
    mobility.SetPositionAllocator(positions);
    mobility.SetMobilityModel("ns3::ConstantVelocityMobilityModel");
    mobility.Install(nodes);

    // Explicit propagation parameters for reproducibility.
    YansWifiChannelHelper channel;
    channel.SetPropagationDelay("ns3::ConstantSpeedPropagationDelayModel");
    channel.AddPropagationLoss(
        "ns3::LogDistancePropagationLossModel",
        "Exponent", DoubleValue(3.0),
        "ReferenceDistance", DoubleValue(1.0),
        "ReferenceLoss", DoubleValue(46.6777));

    YansWifiPhyHelper phy;
    phy.SetChannel(channel.Create());

    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211a);
    wifi.SetRemoteStationManager(
        "ns3::ConstantRateWifiManager",
        "DataMode", StringValue("OfdmRate6Mbps"),
        "ControlMode", StringValue("OfdmRate6Mbps"));

    WifiMacHelper mac;
    mac.SetType("ns3::AdhocWifiMac");
    auto devices = wifi.Install(phy, mac, nodes);

    // Observe relay transmissions at both endpoints.
    for (uint32_t observerId : {0u, 3u})
    {
        auto observerWifi =
            DynamicCast<WifiNetDevice>(devices.Get(observerId));

        for (uint32_t relayId : {1u, 2u})
        {
            auto relayMac =
                Mac48Address::ConvertFrom(devices.Get(relayId)->GetAddress());

            const bool connected =
                observerWifi->GetPhy()->TraceConnectWithoutContext(
                    "MonitorSnifferRx",
                    MakeBoundCallback(&RecordLinkSignal,
                                      observerId,
                                      relayId,
                                      relayMac));

            NS_ABORT_MSG_IF(!connected, "Could not connect link signal trace");
        }
    }


    // Standard AODV performs routing during this validation.
    AodvHelper aodv;
    InternetStackHelper stack;
    Ipv4StaticRoutingHelper staticHelper;
    Ipv4ListRoutingHelper routingList;
    if (adaptiveForwarding)
    {
        routingList.Add(staticHelper, 100);
        routingList.Add(aodv, 0);
        stack.SetRoutingHelper(routingList);
    }
    else
    {
        stack.SetRoutingHelper(aodv);
    }
    stack.Install(nodes);

    Ipv4AddressHelper addresses;
    addresses.SetBase("10.0.0.0", "255.255.255.0");
    auto interfaces = addresses.Assign(devices);
    destinationAddress = interfaces.GetAddress(3);

    if (adaptiveForwarding)
    {
        // Static routing automatically creates connected-subnet routes.
        // Remove these so they cannot bypass AODV for other destinations.
        for (uint32_t id = 0; id < nodes.GetN(); ++id)
        {
            auto ipv4 = nodes.Get(id)->GetObject<Ipv4>();
            auto routes = staticHelper.GetStaticRouting(ipv4);
            NS_ABORT_MSG_IF(!routes, "Missing prototype static routing");

            while (routes->GetNRoutes() > 0)
            {
                routes->RemoveRoute(routes->GetNRoutes() - 1);
            }

            const int32_t interface = ipv4->GetInterfaceForDevice(devices.Get(id));
            NS_ABORT_MSG_IF(interface < 0, "Missing Wi-Fi IPv4 interface");

            if (id == 0)
            {
                sourceRoutes = routes;
                sourceInterface = static_cast<uint32_t>(interface);
            }
            else if (id == 1 || id == 2)
            {
                relayAddresses[id] = interfaces.GetAddress(id);
                routes->AddHostRouteTo(
                    destinationAddress, static_cast<uint32_t>(interface));
            }
        }
    }

    for (uint32_t id = 0; id < nodes.GetN(); ++id)
    {
        auto ipv4 = nodes.Get(id)->GetObject<Ipv4L3Protocol>();
        const bool traced = ipv4->TraceConnectWithoutContext(
            "UnicastForward", MakeBoundCallback(&RecordForward, id));
        NS_ABORT_MSG_IF(!traced, "Could not connect IP forwarding trace");
    }

    std::cout << "FORWARDING_CONFIG adaptive=" << adaptiveForwarding
              << " configured_onward_routes=" << adaptiveForwarding
              << '\n';

    auto routes = Create<OutputStreamWrapper>("aodv.routes", std::ios::out);
    for (int time = 8; time <= 29; ++time)
    {
        Ipv4RoutingHelper::PrintRoutingTableAllAt(Seconds(time), routes);
    }

    constexpr uint16_t port = 9000;
    constexpr uint32_t packetSize = 128;

    UdpServerHelper server(port);
    auto receivers = server.Install(nodes.Get(3));
    auto receiver = receivers.Get(0)->GetObject<UdpServer>();
    const bool connected = receiver->TraceConnectWithoutContext(
        "Rx", MakeCallback(&RecordArrival));
    NS_ABORT_MSG_IF(!connected, "Could not connect UDP arrival trace");
    receivers.Start(Seconds(0));

    UdpClientHelper client(interfaces.GetAddress(3), port);
    client.SetAttribute("PacketSize", UintegerValue(packetSize));
    client.SetAttribute("Interval", TimeValue(Seconds(1)));
    client.SetAttribute("MaxPackets", UintegerValue(30));
    auto senders = client.Install(nodes.Get(0));
    auto sender = senders.Get(0)->GetObject<UdpClient>();
    senders.Start(Seconds(0.5));

    std::cout << "CONFIG activeRelay=" << activeRelay
              << " moveRelay=" << moveRelay
              << " speed_mps=5 movement_start_s=10"
              << " duration_s=30 seed=12345 run=1\n";

    for (uint32_t id = 0; id < nodes.GetN(); ++id)
    {
        auto position = nodes.Get(id)->GetObject<MobilityModel>()->GetPosition();
        std::cout << "POSITION node=" << id
                  << " x_m=" << position.x
                  << " y_m=" << position.y << '\n';
    }

    if (moveRelay == 1)
    {
        auto relay = nodes.Get(1)->GetObject<ConstantVelocityMobilityModel>();
        Simulator::Schedule(Seconds(10),
                            &ConstantVelocityMobilityModel::SetVelocity,
                            relay,
                            Vector(0.0, 5.0, 0.0));
    }

    for (int time = 8; time <= 29; ++time)
    {
        Simulator::Schedule(Seconds(time), [nodes]() {
            auto sourceMobility = nodes.Get(0)->GetObject<MobilityModel>();
            auto destinationMobility = nodes.Get(3)->GetObject<MobilityModel>();

            for (uint32_t id : {1u, 2u})
            {
                auto relay = nodes.Get(id)->GetObject<MobilityModel>();
                auto position = relay->GetPosition();

                std::cout << std::fixed << std::setprecision(6)
                          << "RELAY_POSITION"
                          << " t_s=" << Simulator::Now().GetSeconds()
                          << " relay=" << id
                          << " x_m=" << position.x
                          << " y_m=" << position.y
                          << " source_distance_m="
                          << relay->GetDistanceFrom(sourceMobility)
                          << " destination_distance_m="
                          << relay->GetDistanceFrom(destinationMobility)
                          << '\n';
            }
        });
    }

    scoreFile.open("live-candidate-scores.csv");
    NS_ABORT_MSG_IF(!scoreFile.is_open(), "Could not open scoring CSV");
    scoreFile << std::setprecision(17);
    scoreFile
        << "time_s,relay,status,age_s,signal_dbm,samples,"
        << "weakening_db_s,quality,stability,quality_weight,"
        << "stability_weight,fixed_score,adaptive_score,"
        << "strongest_choice,fixed_choice,adaptive_choice\n";

    // Schedule all evaluations before the stop event, including t=30.
    for (int time = 1; time <= 30; ++time)
    {
        Simulator::Schedule(Seconds(time), &EvaluateCandidates);
    }

    Simulator::Stop(Seconds(30));
    Simulator::Run();

    const auto sent = sender->GetTotalTx() / packetSize;
    const auto received = receiver->GetReceived();

    std::cout << "SUMMARY adaptiveForwarding=" << adaptiveForwarding
              << " moveRelay=" << moveRelay
              << " sent=" << sent
              << " received=" << received
              << " pdr_percent="
              << (sent ? 100.0 * received / sent : 0.0)
              << '\n';

    scoreFile.flush();
    NS_ABORT_MSG_IF(!scoreFile.good(), "Failed to write scoring CSV");
    scoreFile.close();

    Simulator::Destroy();
    return 0;
}
