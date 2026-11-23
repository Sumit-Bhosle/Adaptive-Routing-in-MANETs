// SPDX-License-Identifier: GPL-2.0-only
#include "ns3/aodv-module.h"
#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/wifi-module.h"
#include "ns3/llc-snap-header.h"

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

// One-hop broadcast probes, independent of application-data forwarding.
constexpr uint32_t PROBE_BYTES = 64;
constexpr uint32_t PROBE_COUNT = 60;
constexpr double PROBE_INTERVAL = 0.5;

static std::array<uint32_t, 3> probeAttempts{};
static std::array<uint32_t, 3> probeAccepted{};
static std::array<uint32_t, 3> probeReceived{};

// Destination-side evidence only: not supplied to the source selector.
static std::array<uint32_t, 3> onwardReceived{};
static std::array<double, 3> onwardLastTime{-1.0, -1.0, -1.0};

static void
ReceiveOnwardProbe(uint32_t relayId,
                   Ipv4Address expectedRelayAddress,
                   Ptr<Socket> socket)
{
    Address from;
    while (auto packet = socket->RecvFrom(from))
    {
        if (!InetSocketAddress::IsMatchingType(from))
        {
            continue;
        }

        const auto sender = InetSocketAddress::ConvertFrom(from);
        if (sender.GetIpv4() != expectedRelayAddress ||
            sender.GetPort() != 9100 + relayId ||
            packet->GetSize() != PROBE_BYTES)
        {
            continue;
        }

        SeqTsHeader header;
        if (packet->PeekHeader(header) == 0)
        {
            continue;
        }

        const double now = Simulator::Now().GetSeconds();
        ++onwardReceived.at(relayId);
        onwardLastTime.at(relayId) = now;

        std::cout << std::fixed << std::setprecision(6)
                  << "ONWARD_RX t_s=" << now
                  << " relay=" << relayId
                  << " destination=3"
                  << " probe_seq=" << header.GetSeq()
                  << " delay_ms="
                  << (Simulator::Now() - header.GetTs()).GetSeconds() * 1000.0
                  << '\n';
    }
}


static void
SendNeighbourProbe(Ptr<Socket> socket, uint32_t relayId, uint32_t sequence)
{
    SeqTsHeader header;
    header.SetSeq(sequence);

    auto packet = Create<Packet>(PROBE_BYTES - header.GetSerializedSize());
    packet->AddHeader(header);

    ++probeAttempts.at(relayId);
    const int sent = socket->Send(packet);
    if (sent == static_cast<int>(PROBE_BYTES))
    {
        ++probeAccepted.at(relayId);
    }

    std::cout << std::fixed << std::setprecision(6)
              << "PROBE_TX t_s=" << Simulator::Now().GetSeconds()
              << " relay=" << relayId
              << " seq=" << sequence
              << " udp_payload_bytes=" << PROBE_BYTES
              << " socket_accepted=" << (sent == static_cast<int>(PROBE_BYTES))
              << '\n';

    if (sequence + 1 < PROBE_COUNT)
    {
        Simulator::Schedule(Seconds(PROBE_INTERVAL),
                            &SendNeighbourProbe,
                            socket,
                            relayId,
                            sequence + 1);
    }
}

static void
ReceiveNeighbourProbe(uint32_t relayId, Ptr<Socket> socket)
{
    Address from;
    while (auto packet = socket->RecvFrom(from))
    {
        SeqTsHeader header;
        if (packet->GetSize() != PROBE_BYTES ||
            packet->PeekHeader(header) == 0)
        {
            continue;
        }

        ++probeReceived.at(relayId);
        std::cout << std::fixed << std::setprecision(6)
                  << "PROBE_RX t_s=" << Simulator::Now().GetSeconds()
                  << " relay=" << relayId
                  << " observer=0"
                  << " seq=" << header.GetSeq()
                  << '\n';
    }
}


// Controlled prototype: relay onward routes are configured in advance.
// This switch enables selector-controlled forwarding for any rule.
static bool adaptiveForwarding = false;
static uint32_t selectionRule = 2;

static const char*
SelectionName()
{
    switch (selectionRule)
    {
    case 0:
        return "strongest";
    case 1:
        return "fixed";
    default:
        return "adaptive";
    }
}
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
              << " rule=" << SelectionName()
              << " mode=" << (relayId ? "selected-route" : "AODV-fallback")
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
              << (adaptiveForwarding ? SelectionName() : "AODV")
              << '\n';

    ApplyRelayChoice(choices.at(selectionRule));
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

    bool isNeighbourProbe = false;
    uint32_t probeSequence = 0;

    if (observerId == 0)
    {
        auto copy = packet->Copy();
        WifiMacHeader wifiHeader;
        LlcSnapHeader llc;
        Ipv4Header ip;
        UdpHeader udp;

        if (copy->RemoveHeader(wifiHeader) != 0 &&
            copy->GetSize() >= llc.GetSerializedSize() &&
            copy->RemoveHeader(llc) != 0 &&
            llc.GetType() == 0x0800 &&
            copy->GetSize() >= 20 &&
            copy->RemoveHeader(ip) != 0 &&
            ip.GetProtocol() == 17 &&
            ip.GetFragmentOffset() == 0 &&
            ip.GetDestination() == Ipv4Address("10.0.0.255") &&
            copy->GetSize() >= udp.GetSerializedSize() &&
            copy->RemoveHeader(udp) != 0 &&
            udp.GetDestinationPort() == 9100 + relayId)
        {
            SeqTsHeader probeHeader;
            if (copy->GetSize() >= PROBE_BYTES &&
                copy->PeekHeader(probeHeader) != 0)
            {
                probeSequence = probeHeader.GetSeq();
                isNeighbourProbe = true;
            }
        }
    }

    if (isNeighbourProbe)
    {
        const double now = Simulator::Now().GetSeconds();
        signalHistory.at(relayId).push_back({now, signalNoise.signal});

        // Preserve precision so Python can replay the same scoring inputs.
        std::cout << std::defaultfloat << std::setprecision(17)
                  << "SIGNAL_RX t_s=" << now
                  << " tx=" << relayId
                  << " rx=0"
                  << " signal_dbm=" << signalNoise.signal
                  << " probe_seq=" << probeSequence << '\n';
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
    double relay2Offset = 10.0;
    double relaySpeed = 5.0;

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
    cmd.AddValue("selectionRule",
                 "0=strongest signal, 1=fixed weights, 2=adaptive weights",
                 selectionRule);
    cmd.AddValue("relay2Offset",
                 "Relay 2 position is (35, -offset), in metres",
                 relay2Offset);
    cmd.AddValue("relaySpeed",
                 "Relay 1 upward speed after 10 s, in metres per second",
                 relaySpeed);
    cmd.Parse(argc, argv);

    NS_ABORT_MSG_IF(
        !std::isfinite(relay2Offset) ||
        relay2Offset <= 0.0 || relay2Offset > 1000.0,
        "relay2Offset must be finite, greater than 0 and at most 1000");

    NS_ABORT_MSG_IF(
        !std::isfinite(relaySpeed) ||
        relaySpeed < 0.0 || relaySpeed > 100.0,
        "relaySpeed must be finite and between 0 and 100");
    NS_ABORT_MSG_IF(selectionRule > 2, "Use --selectionRule=0, 1, or 2");
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
    positions->Add(relay2Nearby ? Vector(35, -relay2Offset, 0)
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

    std::vector<Ptr<Socket>> probeSockets;
    for (uint32_t relayId : {1u, 2u})
    {
        const uint16_t probePort = 9100 + relayId;

        auto receiverSocket = Socket::CreateSocket(
            nodes.Get(0), UdpSocketFactory::GetTypeId());
        NS_ABORT_MSG_IF(
            receiverSocket->Bind(
                InetSocketAddress(Ipv4Address::GetAny(), probePort)) != 0,
            "Could not bind source probe receiver");

        receiverSocket->SetRecvCallback(
            MakeBoundCallback(&ReceiveNeighbourProbe, relayId));
        probeSockets.push_back(receiverSocket);

        // Listen at the destination for the same one-hop relay broadcasts.
        auto destinationSocket = Socket::CreateSocket(
            nodes.Get(3), UdpSocketFactory::GetTypeId());

        NS_ABORT_MSG_IF(
            destinationSocket->Bind(
                InetSocketAddress(Ipv4Address::GetAny(), probePort)) != 0,
            "Could not bind destination probe receiver");

        destinationSocket->SetRecvCallback(
            MakeBoundCallback(&ReceiveOnwardProbe,
                              relayId,
                              interfaces.GetAddress(relayId)));

        probeSockets.push_back(destinationSocket);

        auto senderSocket = Socket::CreateSocket(
            nodes.Get(relayId), UdpSocketFactory::GetTypeId());
        senderSocket->BindToNetDevice(devices.Get(relayId));
        NS_ABORT_MSG_IF(
            senderSocket->Bind(
                InetSocketAddress(interfaces.GetAddress(relayId), probePort)) != 0,
            "Could not bind relay probe sender");

        senderSocket->SetAllowBroadcast(true);
        senderSocket->SetIpTtl(1);
        NS_ABORT_MSG_IF(
            senderSocket->Connect(
                InetSocketAddress(Ipv4Address("10.0.0.255"), probePort)) != 0,
            "Could not configure broadcast probe destination");

        probeSockets.push_back(senderSocket);

        const double start = relayId == 1 ? 0.25 : 0.375;
        Simulator::Schedule(Seconds(start),
                            &SendNeighbourProbe,
                            senderSocket,
                            relayId,
                            0u);
    }

    std::cout << "PROBE_CONFIG interval_s=" << PROBE_INTERVAL
              << " count_per_relay=" << PROBE_COUNT
              << " udp_payload_bytes=" << PROBE_BYTES
              << " ttl=1 scoring_input=identified-probes\n";
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

    std::cout << "FORWARDING_CONFIG rule=" << SelectionName()
              << " selector_enabled=" << adaptiveForwarding
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
              << " speed_mps=" << relaySpeed
              << " relay2_offset_m=" << relay2Offset
              << " movement_start_s=10"
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
                            Vector(0.0, relaySpeed, 0.0));
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

    std::cout << "SUMMARY rule=" << SelectionName()
              << " selector_enabled=" << adaptiveForwarding
              << " moveRelay=" << moveRelay
              << " sent=" << sent
              << " received=" << received
              << " pdr_percent="
              << (sent ? 100.0 * received / sent : 0.0)
              << '\n';

    scoreFile.flush();
    NS_ABORT_MSG_IF(!scoreFile.good(), "Failed to write scoring CSV");
    scoreFile.close();

    for (uint32_t relayId : {1u, 2u})
    {
        std::cout << "PROBE_SUMMARY relay=" << relayId
                  << " attempted=" << probeAttempts[relayId]
                  << " socket_accepted=" << probeAccepted[relayId]
                  << " received_at_source=" << probeReceived[relayId]
                  << '\n';
    }

    for (uint32_t relayId : {1u, 2u})
    {
        std::cout << "ONWARD_SUMMARY relay=" << relayId
                  << " received_at_destination=" << onwardReceived[relayId]
                  << " last_rx_s=" << onwardLastTime[relayId]
                  << '\n';
    }

    for (auto& socket : probeSockets)
    {
        socket->Close();
    }

    Simulator::Destroy();
    return 0;
}
