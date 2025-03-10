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
      std::cout << "  - " << tid.GetName() << std::endl << std::flush;
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

  // Enable pcap tracing on all point-to-point interfaces
  p2p.EnablePcapAll("ndn-interests");

  // ADD CONNECTIVITY VERIFICATION HERE
  // Verify node connectivity
  std::cout << "=== VERIFYING NODE CONNECTIVITY ===" << std::endl << std::flush;
  for (int i = 0; i < nodeCount; ++i) {
    Ptr<Node> node = nodes.Get(i);
    std::cout << "Node " << i << " has " << node->GetNDevices() << " devices" << std::endl;
    
    // Print neighbors
    std::cout << "  Connected to: ";
    for (uint32_t d = 1; d < node->GetNDevices(); ++d) { // Skip device 0 (loopback)
      Ptr<NetDevice> dev = node->GetDevice(d);
      Ptr<Channel> channel = dev->GetChannel();
      if (channel) {
        for (std::size_t j = 0; j < channel->GetNDevices(); ++j) {
          Ptr<NetDevice> otherDev = channel->GetDevice(j);
          if (otherDev != dev) {
            // Print node IDs with explicit indication
            std::cout << "Connected to NODE " << otherDev->GetNode()->GetId() << " ";
          }
        }
      }
    }
    std::cout << std::endl << std::flush;
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
    producerHelper.Install(node);
  }

  // Use GlobalRoutingHelper to compute routes for the /aggregate/<id> prefixes
  ns3::ndn::GlobalRoutingHelper grHelper;
  grHelper.InstallAll();
  // 2. For route registration:
  // To this - using binary format:
  for (int i = 0; i < nodeCount; ++i) {
    // Create a Name with binary format
    ::ndn::Name binName("/aggregate");
    binName.appendNumber(i+1);
    
    // Convert to URI string but PRESERVE the binary encoding
    std::string prefixUri = binName.toUri();
    std::cout << "Registering route: " << prefixUri << " for node " << i << std::endl;
    
    grHelper.AddOrigins(prefixUri, nodes.Get(i));
  }
  grHelper.CalculateRoutes();

  // Install a consumer app on each node to request the sum of all *other* nodes' values
  // When setting up consumers, add debug output
  for (int i = 0; i < nodeCount; ++i) {
    Ptr<Node> node = nodes.Get(i);
    // Build the Interest name containing all other node IDs
    ::ndn::Name interestName("/aggregate");  
    
    for (int j = 0; j < nodeCount; ++j) {
      if (j == i) continue; // exclude itself
      // To this - use binary format
      interestName.appendNumber(j+1);
    }

    std::cout << "Node " << i+1 << " will request: " << interestName.toUri() << std::endl << std::flush;

    ns3::ndn::AppHelper consumerHelper("ns3::ndn::ConsumerCbr");
    // More verbose logging for debugging
    consumerHelper.SetAttribute("Frequency", DoubleValue(1.0));  // Use DoubleValue not StringValue
    // Remove the MaxSeq line completely - it's causing the error
    consumerHelper.SetAttribute("Prefix", StringValue(interestName.toUri()));
    
    ApplicationContainer apps = consumerHelper.Install(node);
    std::cout << "Installed consumer app on node " << i+1 << std::endl << std::flush;
  }


  // Add after installing consumers: set explicit start times
  for (int i = 0; i < nodeCount; ++i) {
    // Consumer should be app index 1 (producer was installed first at index 0)
    Ptr<Application> app = nodes.Get(i)->GetApplication(1);
    app->SetStartTime(Seconds(1.0)); // Explicit start time
    std::cout << "Set consumer on node " << i+1 << " to start at 1.0s" << std::endl << std::flush;
  }

  // Add time markers
  for (int t = 0; t <= 5; t++) {
    Simulator::Schedule(Seconds(t), [t]() {
      std::cout << "Time: " << t << "s" << std::endl << std::flush;
    });
  }

  // Add near the end of main(), before Simulator::Run()
  // These built-in tracers will work reliably
  ns3::ndn::L3RateTracer::InstallAll("rate-trace.txt", Seconds(0.5));
  ns3::ndn::AppDelayTracer::InstallAll("app-delays-trace.txt"); 
  ns3::ndn::CsTracer::InstallAll("cs-trace.txt", Seconds(0.5));

  // Run simulation
  Simulator::Stop(Seconds(5.0));
  Simulator::Run();
  Simulator::Destroy();

  return 0;
}