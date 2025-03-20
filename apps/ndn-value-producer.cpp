#include "ndn-value-producer.hpp"
#include "ns3/log.h"
#include "ns3/string.h"
#include "ns3/integer.h"        // This includes IntegerValue
#include "ns3/type-id.h"        // For TypeId
#include "ns3/ndnSIM/helper/ndn-fib-helper.hpp"
// Add this include at the top with other includes
#include "ns3/ndnSIM/helper/ndn-stack-helper.hpp"
#include <endian.h> // For htobe64

// Remove the non-existent include:
// #include "ns3/integer-value.h"  // For IntegerValue 

// Add this at global scope in ndn-value-producer.cpp (outside of any namespace):

NS_LOG_COMPONENT_DEFINE("ndn.ValueProducer");

namespace ns3 {
namespace ndn {

// Add this line here, just before GetTypeId implementation
NS_OBJECT_ENSURE_REGISTERED(ValueProducer);

// At the top near your other implementations
ns3::ndn::ValueProducer::ValueProducer()
{
  m_nodeId = 0;
  m_interestLifetime = Seconds(2);
  NS_LOG_FUNCTION(this);
}

TypeId
ValueProducer::GetTypeId()
{
  // Use static TypeId to ensure it's created only once
  static TypeId tid = TypeId("ValueProducer")
                      .SetGroupName("ndn")
                      .SetParent<App>()
                      .AddConstructor<ValueProducer>() 
                      .AddAttribute("NodeID", "Node ID value",
                                  IntegerValue(0),
                                  MakeIntegerAccessor(&ValueProducer::m_nodeId),
                                  MakeIntegerChecker<int>())
                      .AddAttribute("Prefix", 
                                  "Interest prefix to send when acting as consumer",
                                  NameValue(),
                                  MakeNameAccessor(&ValueProducer::m_prefix),
                                  MakeNameChecker())
                      .AddAttribute("LifeTime", 
                                  "LifeTime for interest packets",
                                  StringValue("2s"),
                                  MakeTimeAccessor(&ValueProducer::m_interestLifetime),
                                  MakeTimeChecker());
  return tid;
}

void
ValueProducer::StartApplication() 
{
  App::StartApplication();
  // Get this node's ID (set via attribute or use Node's system ID)
  if (m_nodeId == 0) {
    m_nodeId = GetNode()->GetId() + 1; // default to NS-3 node ID if not set
  }
  
  // Register prefix using binary format (BUG FIX)
  ::ndn::Name binName("/aggregate");
  binName.appendNumber(m_nodeId); // Binary format (will create /aggregate/%01, etc.)
  std::string prefixUri = binName.toUri();
  
  FibHelper::AddRoute(GetNode(), prefixUri, m_face, 0);
  NS_LOG_INFO("Node " << m_nodeId << " registered binary prefix: " << prefixUri);

  // If we have a consumer prefix set, schedule sending one interest
  if (m_prefix.size() > 0) {
    Simulator::Schedule(Seconds(1.0), &ValueProducer::SendOneInterest, this);
    std::cout << "Node " << m_nodeId << " will request: " << m_prefix << std::endl;
  }
}

void
ValueProducer::SendOneInterest()
{
  if (!m_active)
    return;
    
  // Create interest
  auto interest = std::make_shared<::ndn::Interest>(m_prefix);
  
  // Convert NS3 Time to NDN Time when setting interest lifetime
  interest->setInterestLifetime(::ndn::time::milliseconds(m_interestLifetime.GetMilliSeconds()));
  
  // Send it
  std::cout << "Node " << m_nodeId << " sending Interest: " << interest->getName() 
            << " at " << std::fixed << std::setprecision(2)
            << Simulator::Now().GetSeconds() << "s" << std::endl;
            
  m_transmittedInterests(interest, this, static_cast<nfd::face::Face*>(m_face.get()));
  m_face->sendInterest(*interest);
}

// Stop application implementation
void
ValueProducer::StopApplication()
{
  NS_LOG_FUNCTION(this);
  App::StopApplication();
}

// Simplified OnInterest implementation to work with AggregateStrategy
void
ValueProducer::OnInterest(std::shared_ptr<const ::ndn::Interest> interest) 
{
  // Get the name first for analysis
  ::ndn::Name interestName = interest->getName();
  uint32_t appFaceId = m_face->getId();
  
  std::cout << "\nNode " << m_nodeId << " received Interest: " << interestName 
            << " via app face " << appFaceId << std::endl;

  // Check if this is an interest specifically for our own data
  bool isSelfDataRequest = false;
  bool isMultiNodeInterest = false;
  
  if (interestName.size() >= 2 && interestName.get(0).toUri() == "aggregate") {
    // Count node ID components to determine if it's multi-node (EXCLUDING seq= components)
    int nodeIdCount = 0;
    for (size_t i = 1; i < interestName.size(); i++) {
      // SKIP sequence number components
      if (interestName.get(i).toUri().find("seq=") != std::string::npos) {
        continue;  // Skip sequence components
      }
      
      try {
        interestName.get(i).toNumber();
        nodeIdCount++;
      }
      catch (const std::exception&) {
        // Not a numeric component
      }
    }
    
    isMultiNodeInterest = (nodeIdCount > 1);
    
    // Check if this is asking specifically for our data - REGARDLESS of sequence component
    // Look for our node ID in any position except after "aggregate"
    for (size_t i = 1; i < interestName.size(); i++) {
      // Skip sequence components
      if (interestName.get(i).toUri().find("seq=") != std::string::npos) {
        continue;
      }
      
      try {
        uint64_t requestedId = interestName.get(i).toNumber();
        if (requestedId == static_cast<uint64_t>(m_nodeId)) {
          // If this is the only node ID in the interest, it's a self data request
          isSelfDataRequest = !isMultiNodeInterest;
          break;
        }
      }
      catch (const std::exception&) {
        // Not a numeric component
      }
    }
  }
  
  // CASE 1: This is a request specifically for our data - we should respond
  if (isSelfDataRequest) {
    std::cout << "* Node " << m_nodeId << " received direct request for its data" << std::endl;
    
    // Create Data packet
    auto data = std::make_shared<::ndn::Data>(interestName);
    
    // Content: 8-byte integer equal to m_nodeId
    uint64_t val = (uint64_t)(m_nodeId);
    uint64_t netVal = htobe64(val);
    
    // Create a buffer for the content
    std::shared_ptr<::ndn::Buffer> buffer = std::make_shared<::ndn::Buffer>(
      reinterpret_cast<const uint8_t*>(&netVal), sizeof(netVal));
    data->setContent(buffer);
    data->setFreshnessPeriod(::ndn::time::seconds(1));
    
    // Sign the data
    ns3::ndn::StackHelper::getKeyChain().sign(*data);
    
    // Send the Data packet - let ForwardingStrategy handle its routing
    m_transmittedDatas(data, this, m_face);
    m_face->sendData(*data);
    
    std::cout << "Node " << m_nodeId << " produced Data with value = " << val 
              << " at " << std::fixed << std::setprecision(2) 
              << ns3::Simulator::Now().GetSeconds() << "s" 
              << std::endl << std::flush;
    
    return; // We've handled this interest
  }

  // NEW (BUG FIX): If the interest exactly matches our self-generated aggregated interest, forward it explicitly.
  if (interestName == m_prefix) {
    std::cout << "* Node " << m_nodeId << " detected self-generated aggregated interest, "
              << "performing explicit forwarding to rack aggregator" << std::endl;
    auto l3proto = GetNode()->GetObject<ns3::ndn::L3Protocol>();
    if (l3proto) {
      auto& fib = l3proto->getForwarder()->getFib();
      for (const auto& entry : fib) {
        if (entry.getPrefix().toUri() == "/aggregate") {
          if (!entry.getNextHops().empty()) {
            auto nh = entry.getNextHops().front();
            nfd::face::Face* nextFace = &nh.getFace();
            std::cout << "Forwarding self-generated aggregated interest via explicit route on Face " 
                      << nextFace->getId() << std::endl;
            m_transmittedInterests(interest, this, nextFace);
            nextFace->sendInterest(*interest);
            return;
          }
        }
      }
    }
    std::cout << "Explicit route not found, falling back to default processing" << std::endl;
  }
  
  // CASE 2: Multi-node interest or other interest - let NDN and strategy handle it
  // Call the base App::OnInterest which will perform regular NDN processing
  // This will let AggregateStrategy handle the forwarding
  std::cout << "* Node " << m_nodeId << " letting strategy handle interest: " 
            << (isMultiNodeInterest ? "multi-node" : "other") << std::endl;
  
  App::OnInterest(interest);
}

// Simplified OnData implementation to work with AggregateStrategy
void
ValueProducer::OnData(std::shared_ptr<const ::ndn::Data> data)
{ 
  ::ndn::Name dataName = data->getName();
  std::cout << "\nNode " << m_nodeId << " received Data: " << dataName << std::endl;
  
  // Check if this is our own data
  bool isSelfProduced = false;
  if (dataName.size() >= 2 && dataName.get(0).toUri() == "aggregate") {
    try {
      uint64_t dataNodeId = dataName.get(1).toNumber();
      if (dataNodeId == static_cast<uint64_t>(m_nodeId)) {
        isSelfProduced = true;
      }
    }
    catch (const std::exception&) {
      // Not a numeric component or not our data
    }
  }
  
  if (isSelfProduced) {
    // This is data we produced - need to EXPLICITLY forward to network
    std::cout << "* Node " << m_nodeId << " received self-produced data - forwarding to network" << std::endl;
    
    // Get access to L3 protocol and faces
    auto l3proto = GetNode()->GetObject<ns3::ndn::L3Protocol>();
    if (!l3proto) {
      std::cout << "ERROR: Could not get L3Protocol!" << std::endl;
      return;
    }
    
    // Find the face that the original interest came in on
    bool dataForwarded = false;
    const nfd::FaceTable& faceTable = l3proto->getFaceTable();
    auto forwarder = l3proto->getForwarder();
    
    // Get PIT entries that match this data
    const auto& pit = forwarder->getPit();
    std::vector<std::shared_ptr<nfd::pit::Entry>> matchingEntries;
    
    // TO THIS SIMPLER VERSION:
    std::cout << "  Looking for PIT entries that match " << dataName << std::endl;
    for (const auto& pitEntry : pit) {
      if (dataName.isPrefixOf(pitEntry.getName()) || pitEntry.getName().isPrefixOf(dataName)) {
        std::cout << "  Found matching PIT entry: " << pitEntry.getName() << std::endl;
        
        // Get in-faces to forward data to
        for (const auto& inRecord : pitEntry.getInRecords()) {
          uint32_t faceId = inRecord.getFace().getId();
          std::cout << "  Forwarding data to face " << faceId << std::endl;
          
          try {
            // Forward data to this face
            auto face = faceTable.get(faceId);
            if (face) {
              face->sendData(*data);
              std::cout << "  Successfully sent data to face " << faceId << std::endl;
              dataForwarded = true;
            }
          } catch (const std::exception& e) {
            std::cout << "  Error sending to face: " << e.what() << std::endl;
          }
        }
      }
    }
    
    if (!dataForwarded) {
      std::cout << "  WARNING: No matching PIT entries found for forwarding" << std::endl;
    }
    
    // Still call App::OnData for normal processing
    App::OnData(data);
    return;
  }
  
  // This is data from other nodes - extract and process the content
  std::cout << "* Node " << m_nodeId << " processing received data" << std::endl;
  
  // Standard processing of data content
  if (data->getContent().value_size() >= sizeof(uint64_t)) {
    uint64_t netBytes;
    std::memcpy(&netBytes, data->getContent().value(), sizeof(uint64_t));
    uint64_t value = be64toh(netBytes);
    
    // For aggregated results with multiple node IDs
    if (dataName.size() >= 3) {
      std::cout << "✓ Node " << m_nodeId << " received AGGREGATED result: " 
                << value << " at " << std::fixed << std::setprecision(2)
                << ns3::Simulator::Now().GetSeconds() << "s" << std::endl;
    } 
    // For single node data
    else {
      std::cout << "Node " << m_nodeId << " received individual value: " 
                << value << " at " << std::fixed << std::setprecision(2)
                << ns3::Simulator::Now().GetSeconds() << "s" << std::endl;
    }
  }
  
  // Let standard NDN processing occur (important for PIT/CS management)
  App::OnData(data);
}

// Add this implementation after your StartApplication method
void
ValueProducer::DebugPitState(const ::ndn::Name& interestName)
{
  std::cout << "PRODUCER " << m_nodeId << ": Before sending data, checking local PIT:" << std::endl;

  // Get the L3Protocol from the node
  auto l3proto = GetNode()->GetObject<ns3::ndn::L3Protocol>();
  if (!l3proto) {
    std::cout << "  ERROR: Could not get L3Protocol from node!" << std::endl;
    return;
  }
  
  // Get the forwarder from the L3Protocol
  auto forwarder = l3proto->getForwarder();
  
  // Now access the PIT
  const auto& pit = forwarder->getPit();
  std::cout << "  Found " << std::distance(pit.begin(), pit.end()) << " total PIT entries" << std::endl;
  
  for (const auto& pitEntry : pit) {
    std::cout << "  PIT entry: " << pitEntry.getName() 
              << " (InFaces=" << pitEntry.getInRecords().size()
              << ", OutFaces=" << pitEntry.getOutRecords().size() 
              << ")" << std::endl;
              
    // For matching entries, print details about each face
    if (pitEntry.getName().isPrefixOf(interestName) || 
        interestName.isPrefixOf(pitEntry.getName())) {
      std::cout << "    MATCH for current interest! Details:" << std::endl;
      
      // Print IN faces
      for (const auto& inRecord : pitEntry.getInRecords()) {
        std::cout << "    IN face: " << inRecord.getFace().getId() << std::endl;
      }
      
      // Print OUT faces
      for (const auto& outRecord : pitEntry.getOutRecords()) {
        std::cout << "    OUT face: " << outRecord.getFace().getId() << std::endl;
      }
    }
  }
}

void
ValueProducer::DebugFibEntries(const std::string& message)
{
  std::cout << "--- FIB DEBUG for Node " << m_nodeId 
            << " at " << std::fixed << std::setprecision(2) 
            << Simulator::Now().GetSeconds() << "s ---" << std::endl;
  std::cout << message << std::endl;

  // Get the L3Protocol from the node
  auto l3proto = GetNode()->GetObject<ns3::ndn::L3Protocol>();
  if (!l3proto) {
    std::cout << "  ERROR: Could not get L3Protocol from node!" << std::endl;
    return;
  }
  
  // Get the forwarder from the L3Protocol
  auto forwarder = l3proto->getForwarder();
  
  // Access the FIB
  const auto& fib = forwarder->getFib();
  std::cout << "  Found " << std::distance(fib.begin(), fib.end()) << " total FIB entries" << std::endl;
  
  // Track unique faces seen in FIB entries
  std::set<uint32_t> uniqueFaces;
  
  for (const auto& fibEntry : fib) {
    std::cout << "  FIB entry: " << fibEntry.getPrefix() << std::endl;
    
    // Print nexthops
    for (const auto& nexthop : fibEntry.getNextHops()) {
      std::cout << "    NextHop face: " << nexthop.getFace().getId() 
                << " (cost: " << nexthop.getCost() << ")" << std::endl;
      
      // Keep track of unique faces
      uniqueFaces.insert(nexthop.getFace().getId());
    }
  }
  
  // Print summary of unique faces found in FIB
  std::cout << "  Found " << uniqueFaces.size() << " unique faces in FIB entries:" << std::endl;
  for (uint32_t faceId : uniqueFaces) {
    std::string faceType;
    if (faceId == 0)
      faceType = "internal";
    else if (faceId < 256)
      faceType = "system";
    else
      faceType = "app/net";
      
    std::cout << "    Face ID: " << faceId << " (" << faceType << ")" << std::endl;
  }
  
  std::cout << "--- END FIB DEBUG ---" << std::endl;
}

void
ValueProducer::DebugFaceStats()
{
  std::cout << "\n----- FACE STATS FOR NODE " << m_nodeId << " -----" << std::endl;
  
  auto l3proto = GetNode()->GetObject<ns3::ndn::L3Protocol>();
  if (!l3proto) {
    std::cout << "  ERROR: Could not get L3Protocol" << std::endl;
    return;
  }
  
  // Iterate through all faces (using the correct type)
  const nfd::FaceTable& faceTable = l3proto->getFaceTable();
  std::cout << "  Face Table size: " << faceTable.size() << std::endl;
  
  // Start from 1 (not 0) - face 0 causes segfault
  for (uint32_t faceId = 1; faceId < 300; faceId++) {
    // Use safer access method with try/catch
    std::shared_ptr<nfd::Face> facePtr = nullptr;
    
    try {
      // Check if face exists first with .get() method which doesn't throw
      if (faceTable.get(faceId) != nullptr) {
        facePtr = faceTable.get(faceId)->shared_from_this();
        
        std::cout << "  Face ID: " << faceId << std::endl;
        std::cout << "    nInInterests: " << facePtr->getCounters().nInInterests << std::endl;
        std::cout << "    nOutInterests: " << facePtr->getCounters().nOutInterests << std::endl;
        std::cout << "    nInData: " << facePtr->getCounters().nInData << std::endl;
        std::cout << "    nOutData: " << facePtr->getCounters().nOutData << std::endl;
        
        auto transport = facePtr->getTransport();
        if (transport) {
          std::cout << "    LocalUri: " << transport->getLocalUri() << std::endl;
          std::cout << "    RemoteUri: " << transport->getRemoteUri() << std::endl;
        }
      }
    }
    catch (const std::exception& e) {
      // Skip this face if any error occurs
    }
  }
  
  std::cout << "------------------------------" << std::endl << std::flush;
}

// Add this function to the ValueProducer class:

void
ValueProducer::ForwardDataToNetwork(std::shared_ptr<const ::ndn::Data> data)
{
  std::cout << "Node " << m_nodeId << " attempting explicit forwarding of " 
            << data->getName() << std::endl;
            
  auto l3proto = GetNode()->GetObject<ns3::ndn::L3Protocol>();
  if (!l3proto) {
    std::cout << "ERROR: Could not get L3Protocol!" << std::endl;
    return;
  }
  
  // Get the FaceTable
  const nfd::FaceTable& faceTable = l3proto->getFaceTable();
  std::cout << "  Face Table size: " << faceTable.size() << std::endl;
  
  // We need to use forEachFace instead of trying to iterate directly
  std::shared_ptr<nfd::Face> networkFace;
  uint32_t networkFaceId = 0;
  
  // Use a safe method to loop through faces
  for (const auto& faceId : faceTable) {
    try {
      if (faceId.getId() != m_face->getId()) {  // Skip the app face
        auto transport = faceId.getTransport();
        if (transport) {
          std::string uriStr = transport->getLocalUri().toString();
          if (uriStr.find("netdev://") == 0) {  // Only use network interfaces
            // This is a network face
            networkFace = l3proto->getFaceById(faceId.getId());
            networkFaceId = faceId.getId();
            std::cout << "  Found network interface: " << uriStr 
                      << " (ID: " << networkFaceId << ")" << std::endl;
            break;
          }
        }
      }
    }
    catch (const std::exception& e) {
      std::cout << "  Error processing face: " << e.what() << std::endl;
    }
  }
  
  if (networkFace) {
    std::cout << "Node " << m_nodeId << " EXPLICITLY forwarding data " 
              << data->getName() << " to network face " << networkFaceId 
              << std::endl;
              
    // Send the data packet on the network face
    networkFace->sendData(*data);
    
    // Debug to verify
    Simulator::Schedule(MilliSeconds(10), &ValueProducer::DebugFaceStats, this);
  }
  else {
    std::cout << "ERROR: No suitable network face found for forwarding!" << std::endl;
    
    // List all available faces to help diagnose
    std::cout << "  Available faces:" << std::endl;
    for (const auto& f : faceTable) {
      try {
        auto transport = f.getTransport();
        std::string uriStr = transport ? transport->getLocalUri().toString() : "no-transport";
        std::cout << "    Face ID " << f.getId() << ": " << uriStr << std::endl;
      }
      catch (const std::exception& e) {
        std::cout << "    Face ID " << f.getId() << ": <error: " << e.what() << ">" << std::endl;
      }
    }
  }
}

} // namespace ndn
} // namespace ns3