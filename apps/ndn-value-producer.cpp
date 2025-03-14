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

void
ValueProducer::OnInterest(std::shared_ptr<const ::ndn::Interest> interest) 
{
  // Get the name before calling App::OnInterest so we can analyze it first
  ::ndn::Name interestName = interest->getName();
  uint32_t appFaceId = m_face->getId();
  
  std::cout << "\nNode " << m_nodeId << " received Interest: " << interestName 
            << " via app face " << appFaceId << std::endl;

  // First, check if this is a multi-node aggregation interest
  // It should have format /aggregate/X/Y where X and Y are node IDs
  bool isMultiNodeInterest = false;
  int nodeIdCount = 0;
  
  if (interestName.size() >= 2 && interestName.get(0).toUri() == "aggregate") {
    // Count node ID components
    for (size_t i = 1; i < interestName.size(); i++) {
      try {
        interestName.get(i).toNumber(); // Just try to convert to verify it's a node ID
        nodeIdCount++;
      }
      catch (const std::exception& e) {
        // Not a node ID, might be a sequence or other component
      }
    }
    
    // Add this line to set isMultiNodeInterest based on nodeIdCount
    isMultiNodeInterest = (nodeIdCount > 1);

    // If it has more than one node ID component, it's a multi-node interest
    if (isMultiNodeInterest) {
      std::cout << "Multi-node interest detected, forwarding to aggregator" << std::endl;
      
      // Debug FIB state before forwarding
      DebugFibEntries("BEFORE FORWARDING MULTI-NODE INTEREST: " + interestName.toUri());
      
      // Obtain the L3Protocol from the node
      auto l3proto = GetNode()->GetObject<ns3::ndn::L3Protocol>();
      if (!l3proto) {
        std::cout << "  ERROR: Could not get L3Protocol from node!" << std::endl;
        return;
      }
      
      // Get the forwarder from the L3Protocol (forwarder is now a shared_ptr)
      auto forwarder = l3proto->getForwarder();
      
      // Access the FIB (use -> since forwarder is a shared_ptr)
      const auto& fib = forwarder->getFib();
      std::cout << "  Found " << std::distance(fib.begin(), fib.end()) << " total FIB entries" << std::endl;
              
      // Look for the generic "/aggregate" FIB entry.
      ::ndn::Name genericName("/aggregate");
      const nfd::fib::Entry* genericEntry = nullptr;
      uint32_t chosenFaceId = 0;
      uint32_t bestCost = std::numeric_limits<uint32_t>::max();
      
      for (const auto& entry : fib) {
        if (entry.getPrefix() == genericName) {
          for (const auto& nh : entry.getNextHops()) {
            // Choose a face that is different from the local m_face.
            if (nh.getFace().getId() != m_face->getId() && nh.getCost() < bestCost) {
              bestCost = nh.getCost();
              chosenFaceId = nh.getFace().getId();
              genericEntry = &entry;
            }
          }
        }
      }
      
      // Find the Face pointer corresponding to chosenFaceId.
      Face* outFace = nullptr;
      if (genericEntry) {
        for (const auto& nh : genericEntry->getNextHops()) {
          if (nh.getFace().getId() == chosenFaceId) {
            outFace = const_cast<Face*>(&nh.getFace());
            break;
          }
        }
      }
      
      if (outFace != nullptr) {
        std::cout << "Forwarding Interest on face " << outFace->getId() << std::endl;
        m_transmittedInterests(interest, this, static_cast<nfd::face::Face*>(outFace));
        outFace->sendInterest(*interest);
        DebugPitState(interestName);
      } else {
        std::cout << "No suitable next-hop found; using default face " << m_face->getId() << std::endl;
        m_transmittedInterests(interest, this, static_cast<nfd::face::Face*>(m_face.get()));
        m_face->sendInterest(*interest);
        DebugPitState(interestName);
      }
      
      return;
    }        
  }

  // For single-node interests or other interests, process normally
  App::OnInterest(interest);
  
  // Only respond to interests for this specific node
  if (interestName.size() >= 2 && 
      interestName.get(0).toUri() == "aggregate") {
    
    // Try to extract and compare component 1 (the ID)
    bool isMatch = false;
    
    try {
      // This will work for binary-encoded components like %04
      uint64_t requestedId = interestName.get(1).toNumber();
      isMatch = (requestedId == static_cast<uint64_t>(m_nodeId)); // Adjust for 1-based IDs in interests
      std::cout << "\nChecking if ID " << requestedId << " matches node ID " 
                << m_nodeId  << " (index " << m_nodeId << "): " 
                << (isMatch ? "yes" : "no") << std::endl;
    }
    catch (const std::exception& e) {
      // Fallback to string comparison for non-numeric components
      isMatch = (std::to_string(m_nodeId + 1) == interestName.get(1).toUri());
    }
    
    // Only respond if this is an exact match for our node ID (not a multi-node interest)
    if (isMatch && !isMultiNodeInterest) {
      // Create Data packet with name equal to Interest's name
      auto data = std::make_shared<::ndn::Data>(interestName);
      // Content: 8-byte integer equal to m_nodeId
      uint64_t val = (uint64_t)(m_nodeId); // Use 1-based ID in content too
      uint64_t netVal = htobe64(val);
    
      // Create a buffer for the content
      std::shared_ptr<::ndn::Buffer> buffer = std::make_shared<::ndn::Buffer>(
        reinterpret_cast<const uint8_t*>(&netVal), sizeof(netVal));
      data->setContent(buffer);
      data->setFreshnessPeriod(::ndn::time::seconds(1));  // 1 second freshness
      
      // Sign the data
      ns3::ndn::StackHelper::getKeyChain().sign(*data);
      
      // Debug the PIT state before sending
      DebugPitState(interestName);

      // Send the Data packet
      m_transmittedDatas(data, this, m_face);
      m_face->sendData(*data);
      std::cout << "Node " << m_nodeId << " produced Data for " << interestName.toUri() 
            << " with value = " << val 
            << " (replying via app face " << appFaceId << ")"
            << " at " << std::fixed << std::setprecision(2) 
            << ns3::Simulator::Now().GetSeconds() << "s" 
            << std::endl << std::flush;
    }
  }
}

// BUG-FIX: This function is very important. Without it, the data will not be received successfully.
void
ValueProducer::OnData(std::shared_ptr<const ::ndn::Data> data)
{ 
  // Check if this data was produced by this node by examining first two components
  ::ndn::Name dataName = data->getName();
  bool isSelfProduced = false;
  
  if (dataName.size() >= 2 && dataName.get(0).toUri() == "aggregate") {
    try {
      uint64_t dataNodeId = dataName.get(1).toNumber();
      if (dataNodeId == static_cast<uint64_t>(m_nodeId)) {
        // This is our own data - we MUST forward it through the NDN stack
        std::cout << "Node " << m_nodeId << " detected self-produced data: " << dataName 
                  << " (forwarding to network)" << std::endl;
        isSelfProduced = true;
        
        // For self-produced data, we MUST call App::OnData to ensure proper forwarding
        App::OnData(data);
        // Add this line:
        Simulator::Schedule(MilliSeconds(50), &ValueProducer::DebugFaceStats, this);
        return; // We're done - don't process our own data further
      }
    }
    catch (const std::exception& e) {
      // If component isn't numeric, assume it's not our data
    }
  }

  // If we get here, it's NOT self-produced data
  
  // For non-self-produced data, DO NOT call App::OnData
  // This prevents forwarding loops - the NDN stack already forwarded it to us
  
  // Just process the content directly
  std::cout << "Node " << m_nodeId << " received Data: " << data->getName() 
            << " with content size " << data->getContent().value_size() << " bytes"
            << " at " << std::fixed << std::setprecision(2) 
            << ns3::Simulator::Now().GetSeconds() << "s"
            << std::endl;
            
  // Extract and process the content
  if (data->getContent().value_size() >= sizeof(uint64_t)) {
    uint64_t netBytes;
    std::memcpy(&netBytes, data->getContent().value(), sizeof(uint64_t));
    uint64_t value = be64toh(netBytes);
    std::cout << "Node " << m_nodeId << " extracted value from Data: " 
              << value << " at " << std::fixed << std::setprecision(2)
              << ns3::Simulator::Now().GetSeconds() << "s" << std::endl;
  }
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
  std::cout << "----- FACE STATS FOR NODE " << m_nodeId << " -----" << std::endl;
  
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
  
  std::cout << "------------------------------" << std::endl;
}

} // namespace ndn
} // namespace ns3