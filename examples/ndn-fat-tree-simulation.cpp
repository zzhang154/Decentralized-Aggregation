#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/ndnSIM-module.h"

namespace ns3 {

/**
 * This scenario simulates a very simple network topology:
 *
 *
 *      +----------+     1Mbps      +--------+     1Mbps      +----------+
 *      | consumer | <------------> | router | <------------> | producer |
 *      +----------+         10ms   +--------+          10ms  +----------+
 *
 *
 * Consumer requests data from producer with frequency 10 interests per second
 * (interests contain constantly increasing sequence number).
 *
 * For every received interest, producer replies with a data packet, containing
 * 1024 bytes of virtual payload.
 *
 * To run scenario and see what is happening, use the following command:
 *
 *     NS_LOG=ndn.Consumer:ndn.Producer ./waf --run=ndn-simple
 */

int
main(int argc, char* argv[])
{
  // setting default parameters for PointToPoint links and channels
  Config::SetDefault("ns3::PointToPointNetDevice::DataRate", StringValue("1Mbps"));
  Config::SetDefault("ns3::PointToPointChannel::Delay", StringValue("10ms"));
  Config::SetDefault("ns3::DropTailQueue<Packet>::MaxSize", StringValue("20p"));

  // Read optional command-line parameters (e.g., enable visualizer with ./waf --run=<> --visualize
  CommandLine cmd;
  cmd.Parse(argc, argv);

  // Creating nodes
  NodeContainer nodes;
  nodes.Create(3);

  // Connecting nodes using two links
  PointToPointHelper p2p;
  p2p.Install(nodes.Get(0), nodes.Get(1));
  p2p.Install(nodes.Get(1), nodes.Get(2));

  // Install NDN stack on all nodes
  ndn::StackHelper ndnHelper;
  ndnHelper.SetDefaultRoutes(true);
  ndnHelper.InstallAll();

  // Choosing forwarding strategy
  // ndn::StrategyChoiceHelper::InstallAll("/prefix", "/localhost/nfd/strategy/multicast");
  ndn::StrategyChoiceHelper::InstallAll("/", "/localhost/nfd/strategy/pcon-strategy");

  // Installing applications

  // Consumer
  ndn::AppHelper consumerHelper("ns3::ndn::ConsumerCbr");
  // Consumer will request /prefix/0, /prefix/1, ...
  consumerHelper.SetPrefix("/prefix");
  consumerHelper.SetAttribute("Frequency", StringValue("10")); // 10 interests a second
  auto apps = consumerHelper.Install(nodes.Get(0));                        // first node
  apps.Stop(Seconds(10.0)); // stop the consumer app at 10 seconds mark

  // Producer
  ndn::AppHelper producerHelper("ns3::ndn::Producer");
  // Producer will reply to all requests starting with /prefix
  producerHelper.SetPrefix("/prefix");
  producerHelper.SetAttribute("PayloadSize", StringValue("1024"));
  producerHelper.Install(nodes.Get(2)); // last node

  Simulator::Stop(Seconds(20.0));

  ndn::L3RateTracer::InstallAll("z2h/rate-trace.txt", Seconds(1.0));    // z2h

  Simulator::Run();
  Simulator::Destroy();

  return 0;
}

} // namespace ns3

int
main(int argc, char* argv[])
{
  return ns3::main(argc, argv);
}



// #include "ns3/core-module.h"
// #include "ns3/network-module.h"
// #include "ns3/point-to-point-module.h"
// #include "ns3/ndnSIM-module.h"

// using namespace ns3;

// int 
// main(int argc, char* argv[])
// {
//   // Simulation parameters for Fat-Tree
//   uint32_t k = 4;                     // Fat-Tree parameter (k=4 gives 4 pods)
//   uint32_t pods = k;                  // number of pods
//   uint32_t aggPerPod = k/2;           // number of aggregators per pod
//   uint32_t edgesPerPod = k/2;         // number of edge switches per pod
//   uint32_t hostsPerEdge = k/2;        // number of hosts per edge switch

//   // Create node containers for each layer
//   NodeContainer coreNodes;
//   coreNodes.Create((k/2) * (k/2));    // core nodes = (k/2)^2

//   NodeContainer aggNodesAll;
//   NodeContainer edgeNodesAll;
//   NodeContainer hostNodesAll;

//   // Create aggregator and edge nodes for each pod
//   for (uint32_t i = 0; i < pods; ++i) {
//     NodeContainer aggGroup;
//     NodeContainer edgeGroup;
//     aggGroup.Create(aggPerPod);
//     edgeGroup.Create(edgesPerPod);
//     aggNodesAll.Add(aggGroup);
//     edgeNodesAll.Add(edgeGroup);
//     // Create hosts for each edge in this pod
//     for (uint32_t j = 0; j < edgesPerPod; ++j) {
//       NodeContainer hostGroup;
//       hostGroup.Create(hostsPerEdge);
//       hostNodesAll.Add(hostGroup);
//     }
//   }

//   // Install NDN stack on all nodes
//   ndn::StackHelper ndnHelper;
//   ndnHelper.InstallAll();

//   // Set up point-to-point links with 5Mbps bandwidth and 2ms delay
//   PointToPointHelper p2p;
//   p2p.SetDeviceAttribute("DataRate", StringValue("5Mbps"));
//   p2p.SetChannelAttribute("Delay", StringValue("2ms"));

//   // Connect hosts to edge switches
//   // hostNodesAll is grouped by edge, so we iterate through each edge switch
//   for (uint32_t edgeIndex = 0; edgeIndex < edgeNodesAll.GetN(); ++edgeIndex) {
//     Ptr<Node> edgeNode = edgeNodesAll.Get(edgeIndex);
//     // Each edge has hostsPerEdge hosts; connect each to the edge switch
//     for (uint32_t h = 0; h < hostsPerEdge; ++h) {
//       uint32_t hostIndex = edgeIndex * hostsPerEdge + h;
//       Ptr<Node> hostNode = hostNodesAll.Get(hostIndex);
//       // Create a link between host and edge
//       p2p.Install(edgeNode, hostNode);
//     }
//   }

//   // Connect edge switches to aggregator switches in each pod
//   for (uint32_t pod = 0; pod < pods; ++pod) {
//     for (uint32_t a = 0; a < aggPerPod; ++a) {
//       for (uint32_t e = 0; e < edgesPerPod; ++e) {
//         uint32_t aggIndex = pod * aggPerPod + a;
//         uint32_t edgeIndex = pod * edgesPerPod + e;
//         Ptr<Node> aggNode = aggNodesAll.Get(aggIndex);
//         Ptr<Node> edgeNode = edgeNodesAll.Get(edgeIndex);
//         p2p.Install(aggNode, edgeNode);
//       }
//     }
//   }

//   // Connect aggregator switches to core switches
//   // Each aggregator in pod connects to k/2 core nodes. 
//   // We distribute core connections such that aggregators in the same pod connect to disjoint core nodes.
//   uint32_t coreCount = coreNodes.GetN();
//   uint32_t coresPerAgg = k/2;
//   for (uint32_t pod = 0; pod < pods; ++pod) {
//     for (uint32_t a = 0; a < aggPerPod; ++a) {
//       Ptr<Node> aggNode = aggNodesAll.Get(pod * aggPerPod + a);
//       // Each aggregator connects to coresPerAgg core nodes:
//       for (uint32_t c = 0; c < coresPerAgg; ++c) {
//         // core index calculation: 
//         uint32_t coreIndex = a * coresPerAgg + c;
//         if (coreIndex < coreCount) {
//           Ptr<Node> coreNode = coreNodes.Get(coreIndex);
//           p2p.Install(aggNode, coreNode);
//         }
//       }
//     }
//   }

//   // Choose one Aggregator node per pod to run the aggregator application
//   ndn::AppHelper aggAppHelper("ns3::ndn::Aggregator");
//   aggAppHelper.SetAttribute("Freshness", TimeValue(Seconds(1.0)));
//   for (uint32_t pod = 0; pod < pods; ++pod) {
//     // Use the first aggregator node in each pod (index 0 in that pod group)
//     uint32_t aggNodeIndex = pod * aggPerPod; 
//     Ptr<Node> aggNode = aggNodesAll.Get(aggNodeIndex);
//     // Set the prefix as /agg/pod<podNumber>
//     std::string aggPrefix = "/agg/pod" + std::to_string(pod);
//     aggAppHelper.SetAttribute("Prefix", StringValue(aggPrefix));
//     // Number of producers in this pod = edgesPerPod * hostsPerEdge
//     uint32_t prodCount = edgesPerPod * hostsPerEdge;
//     aggAppHelper.SetAttribute("ProducerCount", UintegerValue(prodCount));
//     aggAppHelper.Install(aggNode);
//   }

//   // Install Producer applications on all host nodes
//   ndn::AppHelper producerHelper("ns3::ndn::Producer");
//   producerHelper.SetAttribute("PayloadSize", StringValue("1024"));  // 1 KB payload

//   // Assign each host a unique prefix /agg/pod<PodID>/<HostID> that matches aggregator expectations
//   for (uint32_t pod = 0; pod < pods; ++pod) {
//     uint32_t baseEdgeIndex = pod * edgesPerPod;
//     // There are edgesPerPod edges in this pod, each with hostsPerEdge hosts
//     uint32_t hostNum = 1;
//     for (uint32_t e = 0; e < edgesPerPod; ++e) {
//       uint32_t edgeIndex = baseEdgeIndex + e;
//       for (uint32_t h = 0; h < hostsPerEdge; ++h) {
//         uint32_t hostIndex = edgeIndex * hostsPerEdge + h;
//         Ptr<Node> hostNode = hostNodesAll.Get(hostIndex);
//         // Prefix format: /agg/pod<podID>/<hostNum> 
//         std::string hostPrefix = "/agg/pod" + std::to_string(pod) + "/" + std::to_string(hostNum++);
//         producerHelper.SetAttribute("Prefix", StringValue(hostPrefix));
//         producerHelper.Install(hostNode);
//       }
//     }
//   }

//   // Install Consumer applications (one per pod, requesting that pod's aggregated data)
//   ndn::AppHelper consumerHelper("ns3::ndn::ConsumerCbr");
//   consumerHelper.SetAttribute("Frequency", StringValue("1"));      // 1 interest per second
//   consumerHelper.SetAttribute("MaxSeq", UintegerValue(1));         // only request one Interest
//   for (uint32_t pod = 0; pod < pods; ++pod) {
//     // Place the consumer on a core node (distributed across core nodes for diversity)
//     Ptr<Node> consumerNode = coreNodes.Get(pod % coreNodes.GetN());
//     std::string aggPrefix = "/agg/pod" + std::to_string(pod);
//     consumerHelper.SetPrefix(aggPrefix);
//     consumerHelper.Install(consumerNode);
//   }

//   // Use BestRoute strategy (or multicast) for forwarding
//   ndn::StrategyChoiceHelper::InstallAll("/agg", "/localhost/nfd/strategy/best-route");

//   // Set up global routing (to populate FIBs)
//   ndn::GlobalRoutingHelper grHelper;
//   grHelper.InstallAll();
//   // Advertise prefixes for each producer and each aggregator
//   // (Producers' prefixes and aggregators' top-level prefix)
//   for (uint32_t pod = 0; pod < pods; ++pod) {
//     // Aggregator prefix
//     std::string aggPrefix = "/agg/pod" + std::to_string(pod);
//     Ptr<Node> aggNode = aggNodesAll.Get(pod * aggPerPod);
//     grHelper.AddOrigins(aggPrefix, aggNode);
//     // Hosts prefixes
//     uint32_t baseEdgeIndex = pod * edgesPerPod;
//     uint32_t hostNum = 1;
//     for (uint32_t e = 0; e < edgesPerPod; ++e) {
//       uint32_t edgeIndex = baseEdgeIndex + e;
//       for (uint32_t h = 0; h < hostsPerEdge; ++h) {
//         uint32_t hostIndex = edgeIndex * hostsPerEdge + h;
//         Ptr<Node> hostNode = hostNodesAll.Get(hostIndex);
//         std::string hostPrefix = "/agg/pod" + std::to_string(pod) + "/" + std::to_string(hostNum++);
//         grHelper.AddOrigins(hostPrefix, hostNode);
//       }
//     }
//   }
//   ndn::GlobalRoutingHelper::CalculateRoutes();

//   // Tracers to measure overhead and completion time
//   ndn::L3RateTracer::InstallAll("ndn-fat-tree-rate-trace.txt", Seconds(1.0));
//   ndn::AppDelayTracer::InstallAll("ndn-fat-tree-app-delays.txt");

//   Simulator::Stop(Seconds(5.0));
//   Simulator::Run();
//   Simulator::Destroy();

//   return 0;
// }
