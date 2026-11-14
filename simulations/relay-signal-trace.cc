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

static void
RecordRelaySignal(Mac48Address sourceMac,
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
    if (header.GetAddr2() != sourceMac)
    {
        return;
    }

    std::cout << "SIGNAL_RX"
              << " t_s=" << Simulator::Now().GetSeconds()
              << " tx=0 rx=1"
              << " mac_seq=" << header.GetSequenceNumber()
              << " signal_dbm=" << signalNoise.signal
              << '\n';
}

int
main(int argc, char* argv[])
{
    uint32_t moveRelay = 0;
    uint32_t targetNode = 3;
    bool nearRelay = false;
    CommandLine cmd(__FILE__);
    cmd.AddValue("moveRelay", "0 keeps both relays in place; 1 or 2 moves that relay at 10 s", moveRelay);
    cmd.AddValue("targetNode", "UDP receiver: 1 or 2 for a relay, 3 for the destination", targetNode); 
    cmd.AddValue("nearRelay", "Place relay 1 five metres from the sender", nearRelay);
    cmd.Parse(argc, argv);
    NS_ABORT_MSG_IF(targetNode < 1 || targetNode > 3, "Use --targetNode=1, 2, or 3");
    NS_ABORT_MSG_IF(moveRelay > 2, "Use --moveRelay=0, 1, or 2");
    RngSeedManager::SetSeed(12345);
    RngSeedManager::SetRun(1);

    NodeContainer nodes;
    nodes.Create(4);
    auto positions = CreateObject<ListPositionAllocator>();
    positions->Add(Vector(0, 0, 0));     // source
    positions->Add(nearRelay ? Vector(5, 0, 0) : Vector(50, 35, 0));   // relay 1
    positions->Add(Vector(50, -35, 0));  // relay 2
    positions->Add(Vector(100, 0, 0));   // destination
    MobilityHelper mobility;
    mobility.SetPositionAllocator(positions);
    mobility.SetMobilityModel("ns3::ConstantVelocityMobilityModel");
    mobility.Install(nodes);

    YansWifiChannelHelper channel;
    channel.SetPropagationDelay("ns3::ConstantSpeedPropagationDelayModel");
    channel.AddPropagationLoss("ns3::LogDistancePropagationLossModel");
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
    auto sourceMac = Mac48Address::ConvertFrom(devices.Get(0)->GetAddress());
    auto relayWifi = DynamicCast<WifiNetDevice>(devices.Get(1));

    bool traceConnected = relayWifi->GetPhy()->TraceConnectWithoutContext(
    	"MonitorSnifferRx",
    	MakeBoundCallback(&RecordRelaySignal, sourceMac));
    NS_ABORT_MSG_IF(!traceConnected, "Could not connect relay 1 signal trace");
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
    auto receivers = server.Install(nodes.Get(targetNode));
    auto receiver = receivers.Get(0)->GetObject<UdpServer>();
    receiver->TraceConnectWithoutContext("Rx", MakeCallback(&RecordArrival));
    receivers.Start(Seconds(0));
    UdpClientHelper client(interfaces.GetAddress(targetNode), port);
    client.SetAttribute("PacketSize", UintegerValue(packetSize));
    client.SetAttribute("Interval", TimeValue(Seconds(1)));
    client.SetAttribute("MaxPackets", UintegerValue(30));
    auto senders = client.Install(nodes.Get(0));
    auto sender = senders.Get(0)->GetObject<UdpClient>();
    senders.Start(Seconds(0.5));

    if (moveRelay)
    {
    	auto relay = nodes.Get(moveRelay)->GetObject<ConstantVelocityMobilityModel>();
    	Simulator::Schedule(Seconds(10),
                        &ConstantVelocityMobilityModel::SetVelocity,
                        relay,
                        Vector(5.0, 0.0, 0.0));
    }
	for (double time : {8.0, 12.0, 20.0})
	{
	    Simulator::Schedule(Seconds(time), [nodes]() {
	        auto source = nodes.Get(0)->GetObject<MobilityModel>();

	        for (uint32_t id : {1u, 2u})
	        {
	            auto relay = nodes.Get(id)->GetObject<MobilityModel>();
	            auto position = relay->GetPosition();

	            std::cout << "POSITION"
	                      << " t_s=" << Simulator::Now().GetSeconds()
	                      << " relay=" << id
	                      << " x_m=" << position.x
	                      << " y_m=" << position.y
	                      << " distance_to_source_m=" << relay->GetDistanceFrom(source)
	                      << '\n';
	        }
	    });
	}
    Simulator::Stop(Seconds(30));
    Simulator::Run();
    std::cout << "Relay moved: " << moveRelay << "\nUDP packets sent: "
              << sender->GetTotalTx() / packetSize << "\nUDP packets received: "
              << receiver->GetReceived() << "\n";
    Simulator::Destroy();
}
