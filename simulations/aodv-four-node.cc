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

static void
RecordArrival(Ptr<const Packet> packet)
{
    SeqTsHeader header;
    packet->PeekHeader(header);
    std::cout << "UDP_RX " << std::fixed << std::setprecision(6)
              << Simulator::Now().GetSeconds() << " " << header.GetSeq() << "\n";
}

int
main(int argc, char* argv[])
{
    uint32_t moveRelay = 0;
    CommandLine cmd(__FILE__);
    cmd.AddValue("moveRelay", "0 keeps both relays in place; 1 or 2 moves that relay at 10 s", moveRelay);
    cmd.Parse(argc, argv);
    NS_ABORT_MSG_IF(moveRelay > 2, "Use --moveRelay=0, 1, or 2");
    RngSeedManager::SetSeed(12345);
    RngSeedManager::SetRun(1);

    NodeContainer nodes;
    nodes.Create(4);
    auto positions = CreateObject<ListPositionAllocator>();
    positions->Add(Vector(0, 0, 0));     // source
    positions->Add(Vector(50, 35, 0));   // relay 1
    positions->Add(Vector(50, -35, 0));  // relay 2
    positions->Add(Vector(100, 0, 0));   // destination
    MobilityHelper mobility;
    mobility.SetPositionAllocator(positions);
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(nodes);

    YansWifiChannelHelper channel;
    channel.SetPropagationDelay("ns3::ConstantSpeedPropagationDelayModel");
    channel.AddPropagationLoss("ns3::RangePropagationLossModel", "MaxRange", DoubleValue(65));
    YansWifiPhyHelper phy;
    phy.SetChannel(channel.Create());
    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211a);
    wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager",
                                 "DataMode", StringValue("OfdmRate6Mbps"),
                                 "ControlMode", StringValue("OfdmRate6Mbps"));
    WifiMacHelper mac;
    mac.SetType("ns3::AdhocWifiMac");
    auto devices = wifi.Install(phy, mac, nodes);

    AodvHelper aodv;
    InternetStackHelper stack;
    stack.SetRoutingHelper(aodv);
    stack.Install(nodes);
    Ipv4AddressHelper addresses;
    addresses.SetBase("10.0.0.0", "255.255.255.0");
    auto interfaces = addresses.Assign(devices);
    auto routes = Create<OutputStreamWrapper>("aodv.routes", std::ios::out);
    for (int t : {8, 12, 20, 29})
    {
        Ipv4RoutingHelper::PrintRoutingTableAllAt(Seconds(t), routes);
    }

    constexpr uint16_t port = 9000;
    constexpr uint32_t packetSize = 128;
    UdpServerHelper server(port);
    auto receivers = server.Install(nodes.Get(3));
    auto receiver = receivers.Get(0)->GetObject<UdpServer>();
    receiver->TraceConnectWithoutContext("Rx", MakeCallback(&RecordArrival));
    receivers.Start(Seconds(0));
    UdpClientHelper client(interfaces.GetAddress(3), port);
    client.SetAttribute("PacketSize", UintegerValue(packetSize));
    client.SetAttribute("Interval", TimeValue(Seconds(1)));
    client.SetAttribute("MaxPackets", UintegerValue(30));
    auto senders = client.Install(nodes.Get(0));
    auto sender = senders.Get(0)->GetObject<UdpClient>();
    senders.Start(Seconds(0.5));

    if (moveRelay)
    {
        auto relay = nodes.Get(moveRelay)->GetObject<MobilityModel>();
        Simulator::Schedule(Seconds(10), &MobilityModel::SetPosition, relay, Vector(1000, 1000, 0));
    }
    Simulator::Stop(Seconds(30));
    Simulator::Run();
    std::cout << "Relay moved: " << moveRelay << "\nUDP packets sent: "
              << sender->GetTotalTx() / packetSize << "\nUDP packets received: "
              << receiver->GetReceived() << "\n";
    Simulator::Destroy();
}
