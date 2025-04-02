#include "ndn-aggregate-simulation-helper.hpp"

namespace ns3 {
namespace ndn {

AggregateSimulationHelper::AggregateSimulationHelper()
  : m_nodeCount(5)
{
}

//
// TOPOLOGY MANAGEMENT
//

void
AggregateSimulationHelper::SetNodeCount(int count)
{
  m_nodeCount = count;
}

NodeContainer
AggregateSimulationHelper::CreateTopology()
{
  std::cout << "=== CREATING TOPOLOGY ===" << std::endl;
  
  // Exactly one consumer/producer node per rack as requested
  int numRacks = m_nodeCount; 
  int numRackAggregators = numRacks;
  int numCoreAggregators = (numRacks > 1) ? std::max(1, numRacks / 4) : 0;
  
  int totalNodes = m_nodeCount + numRackAggregators + numCoreAggregators;
  
  std::cout << "Topology configuration:" << std::endl
            << "  " << m_nodeCount << " producer/consumer nodes (1 per rack)" << std::endl
            << "  " << numRacks << " racks" << std::endl
            << "  " << numRackAggregators << " rack-level aggregators" << std::endl
            << "  " << numCoreAggregators << " core aggregators" << std::endl
            << "  " << totalNodes << " total nodes" << std::endl;
  
  // Create all nodes
  NodeContainer nodes;
  nodes.Create(totalNodes);
  
  // Clear and repopulate node IDs
  m_producerIds.clear();
  m_rackAggregatorIds.clear();
  m_coreAggregatorIds.clear();
  
  // Assign node IDs
  for (int i = 0; i < m_nodeCount; i++) {
    m_producerIds.push_back(i);
  }
  
  for (int i = 0; i < numRackAggregators; i++) {
    m_rackAggregatorIds.push_back(m_nodeCount + i);
  }
  
  for (int i = 0; i < numCoreAggregators; i++) {
    m_coreAggregatorIds.push_back(m_nodeCount + numRackAggregators + i);
  }
  
  // Set up network links
  PointToPointHelper p2p;
  p2p.SetChannelAttribute("Delay", StringValue("2ms"));
  p2p.SetDeviceAttribute("DataRate", StringValue("10Gbps"));
  
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
  std::cout << "Producer/Consumer nodes:       Indices 0-" << (m_nodeCount-1)
            << " (Logical IDs 1-" << m_nodeCount << ")" << std::endl;
  std::cout << "Rack Aggregator nodes:         Indices " << m_nodeCount << "-" 
            << (m_nodeCount+numRackAggregators-1) << std::endl;
  std::cout << "Core Aggregator nodes:         Indices " << (m_nodeCount+numRackAggregators) << "-" 
            << (totalNodes-1) << std::endl;
  
  // Store the nodes
  m_nodes = nodes;
  return nodes;
}

void 
AggregateSimulationHelper::PrintTopologyDiagram() const
{
  int numRacks = m_nodeCount;
  int numRackAggregators = m_rackAggregatorIds.size();
  int numCoreAggregators = m_coreAggregatorIds.size();
  
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
  for (int i = 0; i < m_nodeCount; i++) {
    std::cout << "[P" << (i + 1) << "]";
    if (i < m_nodeCount - 1)
      std::cout << std::string(nodeSpacing - 3, ' ');
  }
  std::cout << std::endl << std::endl;
}

const std::vector<int>&
AggregateSimulationHelper::GetProducerIds() const
{
  return m_producerIds;
}

//
// APPLICATION MANAGEMENT
//

void
AggregateSimulationHelper::InstallProducers(const NodeContainer& nodes)
{
  std::cout << "\n=== INSTALLING PRODUCERS ===" << std::endl;
  
  // Install consumer/producer applications
  for (int i = 0; i < m_producerIds.size(); ++i) {
    int nodeId = m_producerIds[i];
    
    // Create ValueProducer for each node
    ns3::ndn::AppHelper producerHelper("ns3::ndn::ValueProducer");

    // Configure producer - use correct attribute names and types
    producerHelper.SetAttribute("NodeID", IntegerValue(i + 1));
    producerHelper.SetAttribute("PayloadSize", IntegerValue(1024)); // 1KB payload, but now actullay apply in value poducer. Need to be fixed after.
    producerHelper.SetAttribute("Freshness", TimeValue(Seconds(10.0)));
    
    // Construct a consumer prefix that includes all other node IDs
    std::string consumerPrefix = "/aggregate";
    for (int j = 1; j <= m_producerIds.size(); ++j) {
        if (j == i + 1) continue; // Skip the local node's own ID
        consumerPrefix += "/" + std::to_string(j);
    }
    producerHelper.SetPrefix(consumerPrefix);
    
    // Install on the node
    producerHelper.Install(nodes.Get(nodeId));
    std::cout << "  Installed ValueProducer on node " << nodeId << " (P" << (i+1) 
              << ") with consumerPrefix " << consumerPrefix << std::endl;
  }
}

void RoutesPropagated() {
    std::cout << "  Route propagation delay completed." << std::endl;
  }

void
AggregateSimulationHelper::ConfigureRouting(const NodeContainer& nodes)
{
  std::cout << "\n=== CONFIGURING ROUTING ===" << std::endl;
  
  // Install global routing helper
  ns3::ndn::GlobalRoutingHelper ndnGlobalRoutingHelper;
  ndnGlobalRoutingHelper.InstallAll();

  // First, add a general /aggregate prefix route to all rack aggregators
  for (uint32_t i = 0; i < m_rackAggregatorIds.size(); ++i) {
    ndnGlobalRoutingHelper.AddOrigin("/aggregate", nodes.Get(m_rackAggregatorIds[i]));
  }
  std::cout << "  Added general /aggregate prefix to all rack aggregators" << std::endl;
  
  // Register prefixes
  for (int i = 0; i < m_producerIds.size(); ++i) {
    int nodeId = m_producerIds[i];
    int producerId = i + 1;  // 1-based ID
    
    // Create the prefix with proper binary encoding
    ::ndn::Name binName("/aggregate");
    binName.appendNumber(producerId);
    
    // Add origin with the binary name
    ndnGlobalRoutingHelper.AddOrigin(binName.toUri(), nodes.Get(nodeId));
    std::cout << "  Added origin for prefix " << binName.toUri() << " on node " << nodeId << std::endl;
    
    // IMPORTANT: Additionally add direct FIB entry for local application
    // BUGS REPORT: We shouldn't use hard coded face ID 256 here.
    // ns3::ndn::FibHelper::AddRoute(nodes.Get(nodeId), binName.toUri(), 
    // static_cast<uint32_t>(256), 0);
    // std::cout << "  Added direct app route for " << binName.toUri() 
    //           << " on node " << nodeId << " to face 256" << std::endl;
  }
  
  // Calculate and install FIBs
  std::cout << "  Calculating and installing all possible routes..." << std::endl;
  ndnGlobalRoutingHelper.CalculateAllPossibleRoutes();
  
  // IMPORTANT: Add a short delay for routes to propagate
  std::cout << "  Waiting for routes to propagate..." << std::endl;
  Simulator::Schedule(MilliSeconds(10), &RoutesPropagated);
}

void
AggregateSimulationHelper::InstallConsumers(const NodeContainer& nodes)
{
    std::cout << "\n=== CONFIGURING CONSUMER BEHAVIOR ON VALUEPRODUCERS ===" << std::endl;
  
    // Configure consumer behavior in the existing ValueProducer apps
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
        
        // Get the existing ValueProducer application
        Ptr<Application> app = node->GetApplication(0);  // First app should be the ValueProducer
        Ptr<ValueProducer> producer = DynamicCast<ValueProducer>(app);
        
        if (producer) {
            // Configure the consumer behavior by setting the prefix
            producer->SetAttribute("Prefix", NameValue(interestName));
            // Don't need to start it separately - StartApplication handles this when prefix is set
            std::cout << "  Configured ValueProducer on node " << consumerId 
                      << " to request: " << interestName.toUri() << std::endl;
        } else {
            NS_FATAL_ERROR("No ValueProducer found on node " << nodeId);
        }
    }
}

//
// MONITORING AND TRACING
//

bool 
AggregateSimulationHelper::ShouldMonitorNode(ns3::ndn::AggregateUtils::NodeRole role)
{
  // Only monitor rack and core aggregator nodes
  return (role == ns3::ndn::AggregateUtils::NodeRole::RACK_AGG || 
          role == ns3::ndn::AggregateUtils::NodeRole::CORE_AGG);
}

void 
AggregateSimulationHelper::ProcessReceivedData(const ::ndn::Data& data, const std::string& roleString, 
                                              uint32_t faceId, Ptr<ns3::ndn::L3Protocol> ndnProtocol)
{
  std::cout << "\n!!! " << roleString << " RECEIVED DATA ON FACE " << faceId 
            << ": " << data.getName() << std::endl;
  
  if (!ndnProtocol) return;
  
  auto forwarder = ndnProtocol->getForwarder();
  if (!forwarder) return;
  
  const auto& pit = forwarder->getPit();
  
  std::cout << "  === PIT STATE ON " << roleString << " ===" << std::endl;
  std::cout << "  Total PIT entries: " << std::distance(pit.begin(), pit.end()) << std::endl;
  
  bool foundMatch = false;
  for (const auto& pitEntry : pit) {
    std::cout << "    PIT entry: " << pitEntry.getName() 
              << " (InFaces=" << pitEntry.getInRecords().size()
              << ", OutFaces=" << pitEntry.getOutRecords().size() 
              << ")" << std::endl;
              
    // Check if data name matches PIT entry name
    if (pitEntry.getName().isPrefixOf(data.getName()) || 
        pitEntry.getName() == data.getName()) {
      foundMatch = true;
      std::cout << "    **** MATCH FOUND **** for data: " << data.getName() << std::endl;
      
      std::cout << "      In faces: ";
      for (const auto& inRecord : pitEntry.getInRecords()) {
        std::cout << inRecord.getFace().getId() << " ";
      }
      std::cout << std::endl;
    }
  }
  
  if (!foundMatch) {
    std::cout << "    **** NO MATCHING PIT ENTRY **** for data: " << data.getName() << std::endl;
    std::cout << "    This data will be dropped by the forwarder" << std::endl;
  }
}

void 
AggregateSimulationHelper::SetupFaceMonitoring(const nfd::Face& face, Ptr<ns3::ndn::L3Protocol> ndnProtocol,
                                              uint32_t nodeId, const std::string& roleString)
{
  uint32_t faceId = face.getId();
  std::cout << "  Setting up monitoring on Face " << faceId << std::endl;
  
  // Create a weak pointer to avoid circular reference
  auto weakNdnProtocol = ndnProtocol;
  
  // Connect handler to afterReceiveData signal 
  face.afterReceiveData.connect(
    [this, weakNdnProtocol, roleString, faceId](const ::ndn::Data& data, const nfd::EndpointId&) {
      // Get a strong reference to the protocol object
      auto protocol = weakNdnProtocol;
      // Process the received data
      ProcessReceivedData(data, roleString, faceId, protocol);
    }
  );
  
  std::cout << "  Data monitoring enabled on " << roleString << ", Face " << faceId << std::endl;
}

void 
AggregateSimulationHelper::SetupNodeMonitoring(Ptr<Node> node, uint32_t nodeIndex, const std::string& roleString)
{
  std::cout << "Setting up monitoring for " << roleString << " (node index " << nodeIndex << ")" << std::endl;
  
  Ptr<ns3::ndn::L3Protocol> ndnProtocol = node->GetObject<ns3::ndn::L3Protocol>();
  if (!ndnProtocol) {
    std::cout << "  No NDN protocol on " << roleString << std::endl;
    return;
  }
  
  // Get a reference to the face table
  const nfd::FaceTable& faceTable = ndnProtocol->getFaceTable();
  
  // Iterate through faces and set up monitoring for each one
  for (const auto& face : faceTable) {
    SetupFaceMonitoring(face, ndnProtocol, nodeIndex, roleString);
  }
}

void 
AggregateSimulationHelper::SetupDataMonitoring()
{
  std::cout << "\n=== ENABLING DATA PACKET MONITORING ===" << std::endl;
  
  for (uint32_t i = 0; i < m_nodes.GetN(); ++i) {
    // Determine node role using the utility class
    auto role = ns3::ndn::AggregateUtils::determineNodeRole(i);
    
    // Skip nodes we don't want to monitor
    // if (!ShouldMonitorNode(role)) {
    //   continue;
    // }
    
    // Get role string with logical ID using the utility function
    std::string roleStr = ns3::ndn::AggregateUtils::getNodeRoleString(role, i);
    
    // Set up monitoring for this node
    SetupNodeMonitoring(m_nodes.Get(i), i, roleStr);
  }
  
  std::cout << "Data packet monitoring enabled for aggregators" << std::endl;
}

void
AggregateSimulationHelper::MacTxTrace(std::string context, Ptr<const Packet> packet)
{
  std::cout << "MAC TX: " << context << " size=" << packet->GetSize() << std::endl;
}

void
AggregateSimulationHelper::MacRxTrace(std::string context, Ptr<const Packet> packet)
{
  std::cout << "MAC RX: " << context << " size=" << packet->GetSize() << std::endl;
}

void 
AggregateSimulationHelper::EnablePacketTracing()
{
  std::cout << "\n=== ENABLING PACKET TRACING ===" << std::endl;
  
  // Connect to all point-to-point devices' trace sources
  Config::Connect("/NodeList/*/DeviceList/*/$ns3::PointToPointNetDevice/MacTx", 
                  MakeCallback(&AggregateSimulationHelper::MacTxTrace));
  Config::Connect("/NodeList/*/DeviceList/*/$ns3::PointToPointNetDevice/MacRx", 
                  MakeCallback(&AggregateSimulationHelper::MacRxTrace));
  
  std::cout << "Packet tracing enabled for all point-to-point links" << std::endl;
}

void
AggregateSimulationHelper::InstallStrategy()
{
  // Get the exact strategy name with version
  ::ndn::Name strategyName = nfd::fw::AggregateStrategy::getStrategyName();
  std::cout << "\n=== INSTALLING STRATEGY ===" << std::endl;
  std::cout << "Strategy name from class: " << strategyName << std::endl;

  // Install with the exact name including version
  ns3::ndn::StrategyChoiceHelper::InstallAll("/aggregate", strategyName.toUri());

  // Then install for specific prefixes with the same exact name
  // for (int i = 1; i <= m_nodeCount; i++) {
  //   ::ndn::Name producerPrefix("/aggregate");
  //   producerPrefix.appendNumber(i);
  //   ns3::ndn::StrategyChoiceHelper::InstallAll(producerPrefix, strategyName.toUri());
  //   std::cout << "  Installing strategy for prefix: " << producerPrefix.toUri() << std::endl;
  // }
}

void
AggregateSimulationHelper::VerifyStrategyInstallation(const NodeContainer& nodes)
{
  // Verify the installation on node 0 as a sample
  Ptr<Node> node0 = nodes.Get(0);
  auto l3Protocol = node0->GetObject<ns3::ndn::L3Protocol>();
  std::cout << "\n=== VERIFYING STRATEGY INSTALLATION ON NODE 0 ===" << std::endl;
  
  if (l3Protocol) {
    auto forwarder = l3Protocol->getForwarder();
    const auto& strategyChoice = forwarder->getStrategyChoice();
    std::cout << "\n=== INSTALLED STRATEGIES ===" << std::endl;
    
    for (int i = 0; i <= m_nodeCount; i++) {
      ::ndn::Name prefix("/aggregate");
      if (i > 0) {
        prefix.appendNumber(i);
      }
      // Retrieve the effective strategy (returns a reference)
      const nfd::fw::Strategy& strategy = strategyChoice.findEffectiveStrategy(prefix);
      // Print the instance name that was previously set via setInstanceName()
      std::cout << "  Prefix: " << prefix.toUri() << " -> Strategy: " 
                << strategy.getInstanceName() << std::endl;
    }
  }
}

void
AggregateSimulationHelper::VerifyFibEntries(const NodeContainer& nodes)
{
  std::cout << "\n=== VERIFYING FIB ENTRIES ===" << std::endl;
  
  for (int i = 0; i < m_nodeCount; i++) {
    Ptr<Node> node = nodes.Get(i);
    Ptr<ns3::ndn::ValueProducer> app = DynamicCast<ns3::ndn::ValueProducer>(node->GetApplication(0));
    if (app) {
      app->PrintFibState("Initial FIB state");
    }
  }
}

void
AggregateSimulationHelper::InstallTracers(const std::string& tracePath)
{
  std::cout << "\n=== INSTALLING TRACERS ===" << std::endl;
  
  // Create directory if it doesn't exist
  std::string mkdirCommand = "mkdir -p " + tracePath;
  if (system(mkdirCommand.c_str()) != 0) {
    NS_LOG_ERROR("Failed to create directory: " << tracePath);
  }
  
  // Install tracers
  ns3::ndn::L3RateTracer::InstallAll(tracePath + "rate-trace.txt", Seconds(0.1));
  ns3::ndn::CsTracer::InstallAll(tracePath + "cs-trace.txt", Seconds(0.5));
  ns3::ndn::AppDelayTracer::InstallAll(tracePath + "app-delays-trace.txt");
  
  std::cout << "Tracers installed in " << tracePath << std::endl;
}

} // namespace ndn
} // namespace ns3