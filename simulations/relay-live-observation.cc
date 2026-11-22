// SPDX-License-Identifier: GPL-2.0-only
#include "ns3/aodv-module.h"
#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/wifi-module.h"

#include <iomanip>
#include <iostream>

using namespace ns3;

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
    stack.SetRoutingHelper(aodv);
    stack.Install(nodes);

    Ipv4AddressHelper addresses;
    addresses.SetBase("10.0.0.0", "255.255.255.0");
    auto interfaces = addresses.Assign(devices);

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

    Simulator::Stop(Seconds(30));
    Simulator::Run();

    const auto sent = sender->GetTotalTx() / packetSize;
    const auto received = receiver->GetReceived();

    std::cout << "SUMMARY moveRelay=" << moveRelay
              << " sent=" << sent
              << " received=" << received
              << " pdr_percent="
              << (sent ? 100.0 * received / sent : 0.0)
              << '\n';

    Simulator::Destroy();
    return 0;
}
