// aggregate-sum-simulation.cpp - Modularized implementation

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/ndnSIM-module.h"

// Include the custom producer
#include "ns3/ndnSIM/apps/ndn-value-producer.hpp"

// Include the custom strategy
#include "ns3/ndnSIM/NFD/daemon/fw/AggregateStrategy.hpp"

using namespace ns3;

// Global variables
int nodeCount = 5;
NodeContainer nodes;

/**
 * Initialize simulation: parse command line args and set up logging
 */
void initializeSimulation(int argc, char* argv[]) {
  std::cout << "=== INITIALIZING SIMULATION ===" << std::endl;
  
  // Parse command line arguments
  CommandLine cmd;
  cmd.AddValue("nodeCount", "Number of nodes in the network", nodeCount);
  cmd.Parse(argc, argv);
  
  // Set up NS-3 logging
  LogComponentEnable("ndn.AggregateStrategy", LOG_LEVEL_INFO);
  LogComponentEnable("ndn.ValueProducer", LOG_LEVEL_INFO);
  
  std::cout << "Node count: " << nodeCount << std::endl;
  
  // Print available TypeIds for debugging
  std::cout << "Available Producer TypeIds:" << std::endl;
  for (uint32_t i = 0; i < TypeId::GetRegisteredN(); i++) {
    TypeId tid = TypeId::GetRegistered(i);
    if (tid.GetName().find("Producer") != std::string::npos) {
      std::cout << "  - " << tid.GetName() << std::endl;
    }
  }
  std::cout << std::endl;
}

/**
 * Create nodes and network topology
 */
void createTopology() {
  std::cout << "=== CREATING TOPOLOGY ===" << std::endl;
  
  // Create nodes
  nodes.Create(nodeCount);
  
  // Set up network links (chain topology)
  PointToPointHelper p2p;
  p2p.SetChannelAttribute("Delay", StringValue("10ms"));
  p2p.SetDeviceAttribute("DataRate", StringValue("1Mbps"));
  
  for (int i = 0; i < nodeCount - 1; ++i) {
    NodeContainer link(nodes.Get(i), nodes.Get(i+1));
    NetDeviceContainer devices = p2p.Install(link);
    std::cout << "  Created link: Node " << i << " ←→ Node " << (i+1) << std::endl;
  }
  
  // Enable pcap tracing
  p2p.EnablePcapAll("ndn-interests");
  
  // Verify connectivity
  std::cout << "=== VERIFYING NODE CONNECTIVITY ===" << std::endl;
  for (int i = 0; i < nodeCount; ++i) {
    Ptr<Node> node = nodes.Get(i);
    std::cout << "Node " << i + 1 << " has " << node->GetNDevices() << " devices" << std::endl;
    
    std::cout << "  Connected to: ";
    for (uint32_t d = 1; d < node->GetNDevices(); ++d) {
      Ptr<NetDevice> dev = node->GetDevice(d);
      Ptr<Channel> channel = dev->GetChannel();
      if (channel) {
        for (std::size_t j = 0; j < channel->GetNDevices(); ++j) {
          Ptr<NetDevice> otherDev = channel->GetDevice(j);
          if (otherDev != dev) {
            std::cout << "NODE " << otherDev->GetNode()->GetId() << " ";
          }
        }
      }
    }
    std::cout << std::endl;
  }
}

/**
 * Install NDN stack and strategy
 */
void installNdnStack() {
  std::cout << "=== INSTALLING NDN STACK ===" << std::endl;
  
  // Install NDN stack on all nodes
  ns3::ndn::StackHelper ndnHelper;

  // Disable content store (CS) completely
  ndnHelper.setCsSize(0); // Use setCsSize instead of SetCsAttribute

  ndnHelper.InstallAll();
  
  // Set the custom AggregateStrategy for the /aggregate prefix
  ns3::ndn::StrategyChoiceHelper::Install(nodes, "/aggregate", 
                                         "/localhost/nfd/strategy/aggregate");
  std::cout << "Installed AggregateStrategy for /aggregate prefix on all nodes" << std::endl;

}

/**
 * Install producer applications
 */
void installProducers() {
  std::cout << "=== INSTALLING PRODUCERS ===" << std::endl;
  
  for (int i = 0; i < nodeCount; ++i) {
    Ptr<Node> node = nodes.Get(i);
    std::cout << "Installing producer on node " << i+1 << std::endl;
    
    ns3::ndn::AppHelper producerHelper("ValueProducer");
    producerHelper.SetAttribute("NodeID", IntegerValue(i+1)); // 1-based ID
    producerHelper.Install(node);
  }
}

/**
 * Configure routing
 */
void configureRouting() {
  std::cout << "=== CONFIGURING ROUTING ===" << std::endl;
  
  ns3::ndn::GlobalRoutingHelper grHelper;
  grHelper.InstallAll();
  
  for (int i = 0; i < nodeCount; ++i) {
    // Create a Name with binary format
    ::ndn::Name binName("/aggregate");
    binName.appendNumber(i+1);
    
    std::string prefixUri = binName.toUri();
    std::cout << "Registering route: " << prefixUri << " for node " << i + 1 << std::endl;
    
    grHelper.AddOrigins(prefixUri, nodes.Get(i));
  }
  
  grHelper.CalculateRoutes();
  std::cout << "Route calculation complete" << std::endl;

  // Debug: Print FIB entries for each node
  for (int i = 0; i < nodeCount; ++i) {
    Ptr<Node> node = nodes.Get(i);
    auto l3 = node->GetObject<ns3::ndn::L3Protocol>();
    if (!l3) continue;
    auto& fib = l3->getForwarder()->getFib();
    std::cout << "Node " << i + 1 << " FIB entries:" << std::endl;
    for (const auto& fibEntry : fib) {
      std::cout << "  - Prefix: " << fibEntry.getPrefix().toUri() << " -> ";
      for (const auto& nh : fibEntry.getNextHops()) {
        std::cout << "Face" << nh.getFace().getId() << " ";
      }
      std::cout << std::endl;
    }
  }
}

/**
 * Install consumer applications
 */
void installConsumers() {
  std::cout << "=== INSTALLING CONSUMERS ===" << std::endl;
  
  for (int i = 0; i < nodeCount; ++i) {
    Ptr<Node> node = nodes.Get(i);
    
    // Build interest name containing all other node IDs
    ::ndn::Name interestName("/aggregate");
    for (int j = 0; j < nodeCount; ++j) {
      if (j == i) continue; // exclude itself
      interestName.appendNumber(j+1);
    }
    
    std::cout << "Node " << i+1 << " will request: " << interestName.toUri() << std::endl;
    
    ns3::ndn::AppHelper consumerHelper("ns3::ndn::ConsumerCbr");
    consumerHelper.SetAttribute("Frequency", DoubleValue(1.0));
    consumerHelper.SetAttribute("Prefix", StringValue(interestName.toUri()));

    // Add this line:
    consumerHelper.SetAttribute("LifeTime", StringValue("2s")); // Longer lifetime for debugging
    
    ApplicationContainer apps = consumerHelper.Install(node);
    std::cout << "Installed consumer app on node " << i+1 << std::endl;
    
    // Set explicit start time
    apps.Start(Seconds(1.0));
    std::cout << "  Consumer will start at 1.0s" << std::endl;
  }
}

/**
 * Configure tracers and simulation parameters
 */
void configureSimulation() {
  std::cout << "=== CONFIGURING SIMULATION ===" << std::endl;
  
  // Add time markers
  for (int t = 0; t <= 5; t++) {
    Simulator::Schedule(Seconds(t), [t]() {
      std::cout << "Time: " << t << "s" << std::endl << std::flush;
    });
  }
  
  // Improved tracer configuration
  std::string tracePath = "results/";
  
  // Create directory using mkdir instead of SystemPath::MakePath
  std::string mkdirCommand = "mkdir -p " + tracePath;
  if (system(mkdirCommand.c_str()) != 0) {
    NS_LOG_ERROR("Failed to create directory: " << tracePath);
  }
  
  // Install L3RateTracer with more frequent sampling (0.1s)
  ns3::ndn::L3RateTracer::InstallAll(tracePath + "rate-trace.txt", Seconds(0.1));
  std::cout << "L3RateTracer installed, output to " << tracePath + "rate-trace.txt" << std::endl;
  
  // Add packet-level tracing for complete visibility
  ns3::ndn::CsTracer::InstallAll(tracePath + "cs-trace.txt", Seconds(0.5));
  ns3::ndn::AppDelayTracer::InstallAll(tracePath + "app-delays-trace.txt");
  
  // Remove L2RateTracer which doesn't exist in this ndnSIM version
  // ns3::ndn::L2RateTracer::InstallAll(tracePath + "drop-trace.txt", Seconds(0.5));
  
  std::cout << "Tracers installed. Simulation will run for 5.0 seconds" << std::endl;
}

/**
 * Main function
 */
int main(int argc, char* argv[]) {
  // Initialize simulation
  initializeSimulation(argc, argv);

  // Create topology and install NDN stack
  createTopology();
  installNdnStack();
  
  // Install applications and configure routing
  installProducers();
  configureRouting();
  installConsumers();
  
  // Configure and run the simulation
  configureSimulation();
  
  Simulator::Stop(Seconds(1.5));
  Simulator::Run();
  Simulator::Destroy();
  
  return 0;
}