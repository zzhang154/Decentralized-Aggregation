// aggregate-sum-simulation.cpp

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/ndnSIM-module.h"

// Include the custom producer
#include "ns3/ndnSIM/apps/ndn-value-producer.hpp"

// Include the custom strategy
#include "ns3/ndnSIM/NFD/daemon/fw/AggregateStrategy.hpp"

using namespace ns3;

int main(int argc, char* argv[]) {
  // Set up NS-3 logging (enable logging for our components)
  LogComponentEnable("ndn.AggregateStrategy", LOG_LEVEL_INFO);
  LogComponentEnable("ndn.ValueProducer", LOG_LEVEL_INFO);  // Updated component name

  // Parse optional command-line parameters (e.g., number of nodes)
  CommandLine cmd;
  int nodeCount = 5;
  cmd.AddValue("nodeCount", "Number of nodes in the network", nodeCount);
  cmd.Parse(argc, argv);

  // ADD TYPEID DEBUG CODE HERE
  std::cout << "Available TypeIds:" << std::endl;
  for (uint32_t i = 0; i < TypeId::GetRegisteredN(); i++) {
    TypeId tid = TypeId::GetRegistered(i);
    if (tid.GetName().find("Producer") != std::string::npos) {
      std::cout << "  - " << tid.GetName() << std::endl;
    }
  }

  // Create nodes
  NodeContainer nodes;
  nodes.Create(nodeCount);

  // Set up a simple topology (e.g., all nodes connected in a line or star)
  // Here we use a simple chain topology for demonstration
  PointToPointHelper p2p;
  p2p.SetChannelAttribute("Delay", StringValue("10ms"));
  p2p.SetDeviceAttribute("DataRate", StringValue("1Mbps"));
  for (int i = 0; i < nodeCount - 1; ++i) {
    NodeContainer link(nodes.Get(i), nodes.Get(i+1));
    NetDeviceContainer devices = p2p.Install(link);
    // Install NDN stack on the point-to-point link
    // BUG FIX: install the NDN stack twice on the same nodes. Delete one of them.
    // ns3::ndn::StackHelper ndnHelper;
    // ndnHelper.Install(link);
  }
  
  // If nodes are not directly connected (like a chain), ensure global routing will find multi-hop paths.
  // Install NDN stack on all nodes (in case some isolated)
  ns3::ndn::StackHelper ndnHelper;
  ndnHelper.InstallAll();

  // Set the custom AggregateStrategy as the forwarding strategy for /aggregate namespace on all nodes
  ns3::ndn::StrategyChoiceHelper::Install(nodes, "/aggregate", "/localhost/nfd/strategy/aggregate");

  // Install the ValueProducer on each node (so each node can respond with its own value)
  for (int i = 0; i < nodeCount; ++i) {
    Ptr<Node> node = nodes.Get(i);
    std::cout << "Installing producer on node " << i+1 << std::endl;
    // Assign NodeID attribute for clarity (not strictly needed if using node->GetId())
    // BUG FIX: use the correct TypeId for the ValueProducer (otherwise, it will not be registered, and report bugs)
    ns3::ndn::AppHelper producerHelper("ValueProducer");
    producerHelper.SetAttribute("NodeID", IntegerValue(i+1)); // use 1-based ID for output clarity
    // producerHelper.Install(node);
  }

  // Use GlobalRoutingHelper to compute routes for the /aggregate/<id> prefixes
  ns3::ndn::GlobalRoutingHelper grHelper;
  grHelper.InstallAll();
  for (int i = 0; i < nodeCount; ++i) {
    std::string prefix = "/aggregate/" + std::to_string(i+1);
    grHelper.AddOrigins(prefix, nodes.Get(i));
  }
  grHelper.CalculateRoutes();

  // Install a consumer app on each node to request the sum of all *other* nodes' values
  for (int i = 0; i < nodeCount; ++i) {
    Ptr<Node> node = nodes.Get(i);
    // Build the Interest name containing all other node IDs
    ::ndn::Name interestName("/aggregate");  // Properly qualify Name
    for (int j = 0; j < nodeCount; ++j) {
      if (j == i) continue; // exclude itself
      interestName.appendNumber(j+1);
    }
    ns3::ndn::AppHelper consumerHelper("ns3::ndn::ConsumerCbr");
    consumerHelper.SetAttribute("Frequency", StringValue("1"));     // 1 interest per second
    consumerHelper.SetAttribute("MaxSeq", IntegerValue(0));         // only send one interest
    consumerHelper.SetAttribute("Prefix", StringValue(interestName.toUri()));
    consumerHelper.Install(node);
  }

  // Run simulation
  Simulator::Stop(Seconds(5.0));
  Simulator::Run();
  Simulator::Destroy();

  return 0;
}