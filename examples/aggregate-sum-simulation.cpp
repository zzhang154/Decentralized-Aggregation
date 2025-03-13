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

// Add these to the global variables section:
std::vector<int> m_producerIds;
std::vector<int> m_rackAggregatorIds;
std::vector<int> m_coreAggregatorIds;


// Declare the NodeCount global value (must be at global scope)
static ns3::GlobalValue g_nodeCount("NodeCount",
  "Number of consumer-producer nodes",
  ns3::UintegerValue(2), // Default value
  ns3::MakeUintegerChecker<uint32_t>(1, 100)); // Min and max values

/**
 * Print ASCII diagram of the network topology with perfect alignment
 */
void printTopologyDiagram(int numRacks, int numRackAggregators, int numCoreAggregators, int nodeCount) {
  std::cout << "\n=== TOPOLOGY DIAGRAM ===" << std::endl;

  const int labelWidth = 17;
  const int nodeSpacing = 7;  // increased spacing for clarity

  // Helper lambda to center connectors
  auto printConnectors = [&](int count, int offset) {
    std::cout << std::string(labelWidth, ' ');
    for (int i = 0; i < count; ++i) {
      std::cout << std::string(offset, ' ') << "|";
      if (i < count - 1)
        std::cout << std::string(nodeSpacing - offset - 1, ' ');
    }
    std::cout << std::endl;
  };

  // Core Layer
  std::cout << std::left << std::setw(labelWidth) << "Core Layer:";
  for (int i = 0; i < numCoreAggregators; i++) {
    std::cout << "[C" << (i + 1) << "]";
    if (i < numCoreAggregators - 1)
      std::cout << std::string(nodeSpacing - 3, ' ');
  }
  std::cout << std::endl;

  // Connectors Core to Rack Aggregators
  printConnectors(numRackAggregators, 1);

  // Rack Aggregators
  std::cout << std::left << std::setw(labelWidth) << "Rack Aggregators:";
  for (int i = 0; i < numRackAggregators; i++) {
    std::cout << "[R" << (i + 1) << "]";
    if (i < numRackAggregators - 1)
      std::cout << std::string(nodeSpacing - 3, ' ');
  }
  std::cout << std::endl;

  // Connectors Rack Aggregators to Producers
  printConnectors(numRacks, 1);

  // Producers
  std::cout << std::left << std::setw(labelWidth) << "Producers:";
  for (int i = 0; i < nodeCount; i++) {
    std::cout << "[P" << (i + 1) << "]";
    if (i < nodeCount - 1)
      std::cout << std::string(nodeSpacing - 3, ' ');
  }
  std::cout << std::endl << std::endl;
}

/**
 * Initialize simulation: parse command line args and set up logging
 */
void initializeSimulation(int argc, char* argv[]) {
  std::cout << "=== INITIALIZING SIMULATION ===" << std::endl;
  
  // Parse command line arguments first
  CommandLine cmd;
  cmd.AddValue("nodeCount", "Number of consumer-producer in the network", nodeCount);
  cmd.Parse(argc, argv);

  // Now bind to the existing global value (it's already been declared above)
  ns3::GlobalValue::Bind("NodeCount", ns3::UintegerValue(nodeCount));
  
  std::cout << "Set global NodeCount=" << nodeCount << std::endl;
  
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
 * Create nodes and data center-like network topology with aggregator nodes
 */
void createTopology() {
  std::cout << "=== CREATING DATA CENTER TOPOLOGY ===" << std::endl;
  
  // Exactly one consumer/producer node per rack as requested
  int numRacks = nodeCount; 
  int numRackAggregators = numRacks;
  int numCoreAggregators = (numRacks > 1) ? std::max(1, numRacks / 4) : 0;
  
  int totalNodes = nodeCount + numRackAggregators + numCoreAggregators;
  
  std::cout << "Topology configuration:" << std::endl
            << "  " << nodeCount << " producer/consumer nodes (1 per rack)" << std::endl
            << "  " << numRacks << " racks" << std::endl
            << "  " << numRackAggregators << " rack-level aggregators" << std::endl
            << "  " << numCoreAggregators << " core aggregators" << std::endl
            << "  " << totalNodes << " total nodes" << std::endl;
  
  // Create all nodes
  nodes.Create(totalNodes);
  
  // Assign node IDs
  for (int i = 0; i < nodeCount; i++) {
    m_producerIds.push_back(i);
  }
  
  for (int i = 0; i < numRackAggregators; i++) {
    m_rackAggregatorIds.push_back(nodeCount + i);
  }
  
  for (int i = 0; i < numCoreAggregators; i++) {
    m_coreAggregatorIds.push_back(nodeCount + numRackAggregators + i);
  }
  
  // Set up network links
  PointToPointHelper p2p;
  p2p.SetChannelAttribute("Delay", StringValue("5ms"));
  p2p.SetDeviceAttribute("DataRate", StringValue("10Gbps"));
  
  // Print ASCII diagram of the topology
  printTopologyDiagram(numRacks, numRackAggregators, numCoreAggregators, nodeCount);
  
  std::cout << "=== CREATING LINKS ===" << std::endl;
  
  // 1. Connect producer nodes to their rack aggregator (1:1 mapping)
  for (int i = 0; i < numRacks; i++) {
    int producerId = m_producerIds[i];
    int rackAggregatorId = m_rackAggregatorIds[i];
    
    NodeContainer link(nodes.Get(producerId), nodes.Get(rackAggregatorId));
    NetDeviceContainer devices = p2p.Install(link);
    std::cout << "  Created link: Producer " << (i + 1) 
              << " ←→ Rack Aggregator " << (i + 1) << std::endl;
  }
  
  // 2. Connect rack aggregators to core aggregators (if applicable)
  if (numCoreAggregators > 0) {
    for (int i = 0; i < numRacks; i++) {
      int rackAggregatorId = m_rackAggregatorIds[i];
      int coreAggregatorId = m_coreAggregatorIds[i % numCoreAggregators];
      
      NodeContainer link(nodes.Get(rackAggregatorId), nodes.Get(coreAggregatorId));
      NetDeviceContainer devices = p2p.Install(link);
      std::cout << "  Created link: Rack Aggregator " << (i + 1) 
                << " ←→ Core Aggregator " << ((i % numCoreAggregators) + 1) << std::endl;
    }
  }
  
  // 3. If multiple core aggregators, connect them in a ring
  if (numCoreAggregators > 1) {
    for (int i = 0; i < numCoreAggregators; i++) {
      int j = (i + 1) % numCoreAggregators;
      int coreId1 = m_coreAggregatorIds[i];
      int coreId2 = m_coreAggregatorIds[j];
      
      NodeContainer link(nodes.Get(coreId1), nodes.Get(coreId2));
      NetDeviceContainer devices = p2p.Install(link);
      std::cout << "  Created link: Core Aggregator " << (i + 1) 
                << " ←→ Core Aggregator " << (j + 1) << std::endl;
    }
  }
  
  // Node index explanation
  std::cout << "\n=== NODE INDEX MAPPING ===" << std::endl;
  std::cout << "Producer/Consumer nodes:       Indices 0-" << (nodeCount-1)
            << " (Logical IDs 1-" << nodeCount << ")" << std::endl;
  std::cout << "Rack Aggregator nodes:         Indices " << nodeCount << "-" 
            << (nodeCount+numRackAggregators-1) << std::endl;
  std::cout << "Core Aggregator nodes:         Indices " << (nodeCount+numRackAggregators) << "-" 
            << (totalNodes-1) << std::endl;
}

/**
 * Install NDN stack and strategy selectively
 */
void installNdnStack() {
  std::cout << "\n=== INSTALLING NDN STACK ===" << std::endl;
  
  // Install basic NDN stack on all nodes
  ns3::ndn::StackHelper ndnHelper;

  // Disable content store (CS) completely
  ndnHelper.setCsSize(0);
  ndnHelper.InstallAll();
  
  // Create node containers for different types
  NodeContainer aggregatorNodes;
  for (int id : m_rackAggregatorIds) {
    aggregatorNodes.Add(nodes.Get(id));
  }
  for (int id : m_coreAggregatorIds) {
    aggregatorNodes.Add(nodes.Get(id));
  }
  
  NodeContainer producerNodes;
  for (int id : m_producerIds) {
    producerNodes.Add(nodes.Get(id));
  }
  
  // Set the custom AggregateStrategy ONLY for aggregator nodes
  ns3::ndn::StrategyChoiceHelper::Install(aggregatorNodes, "/aggregate", 
                                         "/localhost/nfd/strategy/aggregate");
  std::cout << "Installed AggregateStrategy for /aggregate prefix on " 
            << aggregatorNodes.GetN() << " aggregator nodes" << std::endl;

  // Use a simple forwarding strategy for producer/consumer nodes
  ns3::ndn::StrategyChoiceHelper::Install(producerNodes, "/aggregate", 
                                        "/localhost/nfd/strategy/best-route");
  std::cout << "Installed best-route strategy for /aggregate prefix on " 
            << producerNodes.GetN() << " producer/consumer nodes" << std::endl;
}

/**
 * Install producer applications
 */
void installProducers() {
  std::cout << "\n=== INSTALLING PRODUCERS ===" << std::endl;
  
  // Install producers only on regular nodes (not aggregators)
  for (int i = 0; i < m_producerIds.size(); ++i) {
    int nodeId = m_producerIds[i];
    Ptr<Node> node = nodes.Get(nodeId);
    
    // Use 1-based node IDs for consistency with original code
    int producerId = i + 1;
    
    std::cout << "Installing producer on node " << producerId << " (index " << nodeId << ")" << std::endl;
    
    ns3::ndn::AppHelper producerHelper("ValueProducer");
    producerHelper.SetAttribute("NodeID", IntegerValue(producerId));
    producerHelper.Install(node);
  }
}

/**
 * Configure routing
 */
void configureRouting() {
  std::cout << "\n=== CONFIGURING ROUTING ===" << std::endl;
  
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
  std::cout << "\n=== INSTALLING CONSUMERS ===" << std::endl;
  
  // Install consumers only on regular nodes (not aggregators)
  for (int i = 0; i < m_producerIds.size(); ++i) {
    int nodeId = m_producerIds[i];
    Ptr<Node> node = nodes.Get(nodeId);
    
    // Use 1-based node IDs for consistency with original code
    int consumerId = i + 1;
    
    // Build interest name containing all other node IDs
    ::ndn::Name interestName("/aggregate");
    for (int j = 0; j < m_producerIds.size(); ++j) {
      int otherId = j + 1;  // 1-based ID
      if (otherId == consumerId) continue; // exclude itself
      interestName.appendNumber(otherId);
    }
    
    std::cout << "Node " << consumerId << " (index " << nodeId 
              << ") will request: " << interestName.toUri() << std::endl;
    
    ns3::ndn::AppHelper consumerHelper("ns3::ndn::ConsumerCbr");
    consumerHelper.SetAttribute("Frequency", DoubleValue(1.0));
    consumerHelper.SetAttribute("Prefix", StringValue(interestName.toUri()));
    consumerHelper.SetAttribute("LifeTime", StringValue("2s"));
    
    ApplicationContainer apps = consumerHelper.Install(node);
    std::cout << "Installed consumer app on node " << consumerId << std::endl;
    
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