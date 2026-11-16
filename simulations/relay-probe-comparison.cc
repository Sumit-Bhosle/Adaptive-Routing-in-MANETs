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

// Record Wi-Fi data frames received directly from the selected relay.
// These frames can carry application traffic or routing messages.
static void
RecordRelaySignal(uint32_t relayId,
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
              << "SIGNAL_RX"
              << " t_s=" << Simulator::Now().GetSeconds()
              << " tx=" << relayId
              << " rx=0"
              << " mac_seq=" << header.GetSequenceNumber()
              << " signal_dbm=" << signalNoise.signal
              << '\n';
}

int
main(int argc, char* argv[])
{
    uint32_t moveRelay = 0;

    CommandLine cmd(__FILE__);
    cmd.AddValue("moveRelay",
                 "0 keeps both relays stationary; 1 or 2 moves that relay at 10 s",
                 moveRelay);
    cmd.Parse(argc, argv);

    NS_ABORT_MSG_IF(moveRelay > 2, "Use --moveRelay=0, 1, or 2");

    RngSeedManager::SetSeed(12345);
    RngSeedManager::SetRun(1);

    // Both relays start five metres from the observer.
    NodeContainer nodes;
    nodes.Create(4);

    auto positions = CreateObject<ListPositionAllocator>();
    positions->Add(Vector(0, 0, 0));    // node 0: observer
    positions->Add(Vector(5, 0, 0));    // node 1: relay 1
    positions->Add(Vector(0, 5, 0));    // node 2: relay 2
    positions->Add(Vector(100, 0, 0));  // node 3: no application in this experiment

    MobilityHelper mobility;
    mobility.SetPositionAllocator(positions);
    mobility.SetMobilityModel("ns3::ConstantVelocityMobilityModel");
    mobility.Install(nodes);

    // Received power decreases with distance.
    YansWifiChannelHelper channel;
    channel.SetPropagationDelay("ns3::ConstantSpeedPropagationDelayModel");
    channel.AddPropagationLoss("ns3::LogDistancePropagationLossModel");

    YansWifiPhyHelper phy;
    phy.SetChannel(channel.Create());

    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211a);
    wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager",
                                 "DataMode",
                                 StringValue("OfdmRate6Mbps"),
                                 "ControlMode",
                                 StringValue("OfdmRate6Mbps"));

    WifiMacHelper mac;
    mac.SetType("ns3::AdhocWifiMac");

    auto devices = wifi.Install(phy, mac, nodes);

    // Observe each relay's transmissions at node 0.
    auto observerWifi = DynamicCast<WifiNetDevice>(devices.Get(0));

    for (uint32_t relayId : {1u, 2u})
    {
        auto relayMac =
            Mac48Address::ConvertFrom(devices.Get(relayId)->GetAddress());

        bool connected = observerWifi->GetPhy()->TraceConnectWithoutContext(
            "MonitorSnifferRx",
            MakeBoundCallback(&RecordRelaySignal, relayId, relayMac));

        NS_ABORT_MSG_IF(!connected, "Could not connect observer signal trace");
    }

    AodvHelper aodv;
    InternetStackHelper stack;
    stack.SetRoutingHelper(aodv);
    stack.Install(nodes);

    Ipv4AddressHelper addresses;
    addresses.SetBase("10.0.0.0", "255.255.255.0");
    auto interfaces = addresses.Assign(devices);

    auto routes = Create<OutputStreamWrapper>("aodv.routes", std::ios::out);
    for (int time : {8, 12, 20, 29})
    {
        Ipv4RoutingHelper::PrintRoutingTableAllAt(Seconds(time), routes);
    }

    // Each relay sends 30 UDP probes to a separate port on node 0.
    // Stagger the start times to avoid simultaneous scheduled sends.
    constexpr uint32_t packetSize = 128;
    ApplicationContainer probeSenders;
    ApplicationContainer probeReceivers;

    for (uint32_t relayId : {1u, 2u})
    {
        uint16_t port = 9000 + relayId;

        UdpServerHelper server(port);
        auto receivers = server.Install(nodes.Get(0));
        receivers.Start(Seconds(0));
        probeReceivers.Add(receivers);

        UdpClientHelper client(interfaces.GetAddress(0), port);
        client.SetAttribute("PacketSize", UintegerValue(packetSize));
        client.SetAttribute("Interval", TimeValue(Seconds(1)));
        client.SetAttribute("MaxPackets", UintegerValue(30));

        auto senders = client.Install(nodes.Get(relayId));
        senders.Start(Seconds(relayId == 1 ? 0.5 : 0.75));
        probeSenders.Add(senders);
    }

    // The selected relay begins moving at five metres per second.
    if (moveRelay)
    {
        auto relay =
            nodes.Get(moveRelay)->GetObject<ConstantVelocityMobilityModel>();

        Simulator::Schedule(Seconds(10),
                            &ConstantVelocityMobilityModel::SetVelocity,
                            relay,
                            Vector(5.0, 0.0, 0.0));
    }

    for (double time : {8.0, 12.0, 20.0})
    {
        Simulator::Schedule(Seconds(time), [nodes]() {
            auto observer = nodes.Get(0)->GetObject<MobilityModel>();

            for (uint32_t relayId : {1u, 2u})
            {
                auto relay = nodes.Get(relayId)->GetObject<MobilityModel>();
                auto position = relay->GetPosition();

                std::cout << std::fixed << std::setprecision(6)
                          << "POSITION"
                          << " t_s=" << Simulator::Now().GetSeconds()
                          << " relay=" << relayId
                          << " x_m=" << position.x
                          << " y_m=" << position.y
                          << " distance_to_observer_m="
                          << relay->GetDistanceFrom(observer)
                          << '\n';
            }
        });
    }

    Simulator::Stop(Seconds(30));
    Simulator::Run();

    std::cout << "Relay moved: " << moveRelay << '\n';

    for (uint32_t index = 0; index < 2; ++index)
    {
        auto sender = probeSenders.Get(index)->GetObject<UdpClient>();
        auto receiver = probeReceivers.Get(index)->GetObject<UdpServer>();

        std::cout << "PROBE_SUMMARY"
                  << " relay=" << index + 1
                  << " sent=" << sender->GetTotalTx() / packetSize
                  << " received=" << receiver->GetReceived()
                  << '\n';
    }

    Simulator::Destroy();
    return 0;
}
