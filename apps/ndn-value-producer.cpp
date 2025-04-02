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
  m_payloadSize = 1024;
  m_freshness = Seconds(10.0);
  m_seqNo = 0; // Initialize sequence counter
  NS_LOG_FUNCTION(this);
}

TypeId
ValueProducer::GetTypeId()
{
  static TypeId tid = TypeId("ns3::ndn::ValueProducer")
                      .SetGroupName("ndn")
                      .SetParent<App>()
                      .AddConstructor<ValueProducer>() 
                      .AddAttribute("NodeID", "Node ID value",
                                  IntegerValue(0),
                                  MakeIntegerAccessor(&ValueProducer::m_nodeId),
                                  MakeIntegerChecker<int>())
                      .AddAttribute("PayloadSize", "Size of payload in Data packet",
                                  IntegerValue(1024), // Changed from UintegerValue
                                  MakeIntegerAccessor(&ValueProducer::m_payloadSize), // Changed
                                  MakeIntegerChecker<int>()) // Changed and fixed syntax
                      .AddAttribute("Freshness", "Data packet freshness period",
                                  TimeValue(Seconds(10.0)),
                                  MakeTimeAccessor(&ValueProducer::m_freshness),
                                  MakeTimeChecker())
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

void ValueProducer::StartApplication() 
{
  App::StartApplication();
  // Get this node's ID (set via attribute or use Node's system ID)
  if (m_nodeId == 0) {
    m_nodeId = GetNode()->GetId() + 1; // default to NS-3 node ID if not set
  }
  
  // Register prefix using binary format (BUG FIX)
  ::ndn::Name binName("/aggregate");
  binName.appendNumber(m_nodeId); // Binary format (will create /aggregate/<nodeId>)
  std::string prefixUri = binName.toUri();
  
  // Add a FIB entry for the local production prefix
  FibHelper::AddRoute(GetNode(), prefixUri, m_face->getId(), 0);
  
  // [Remove the calls to SetInterestFilter since they are not available in ndnSIM‑2.9]
  // this->SetInterestFilter(binName);
  // ::ndn::Name seqPrefix = ::ndn::Name(binName).append("seq=");
  // this->SetInterestFilter(seqPrefix);
  
  std::cout << "Node " << m_nodeId << " registered prefix (FIB route) for: " 
            << binName.toUri() << std::endl;
  
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
    
  // Create interest with sequence number
  ::ndn::Name interestName = m_prefix;
  
  // Add sequence component - use proper marker
  interestName.append("seq=" + std::to_string(m_seqNo++));
  
  auto interest = std::make_shared<::ndn::Interest>(interestName);
  
  // Convert NS3 Time to NDN Time when setting interest lifetime
  interest->setInterestLifetime(::ndn::time::milliseconds(m_interestLifetime.GetMilliSeconds()));
  
  std::cout << "Node " << m_nodeId << " sending Interest: " 
            << interest->getName() 
            << " at " << std::fixed << std::setprecision(2)
            << Simulator::Now().GetSeconds() << "s" << std::endl;
            
  // Log transmission using your custom transmitter
  m_transmittedInterests(interest, this, static_cast<nfd::face::Face*>(m_face.get()));
  m_face->sendInterest(*interest);
  std::cout << "Interest sent via application face " << m_face->getId() << std::endl;
  
  /*
  // ----- Modification: Send via a network face -----
  Ptr<ns3::ndn::L3Protocol> l3proto = GetNode()->GetObject<ns3::ndn::L3Protocol>();
  bool interestSent = false;
  if (l3proto) {
      // Iterate over the face table to find a network face (not the app face, and not internal face 0)
      const nfd::FaceTable& faceTable = l3proto->getFaceTable();
      for (const auto &face : faceTable) {
          if (face.getId() != m_face->getId() && face.getId() != 0) { // Exclude app face and internal face
              nfd::Face* netFace = const_cast<nfd::Face*>(&face);
              std::cout << "Using network face " << netFace->getId() 
                        << " to send Interest" << std::endl;
              netFace->sendInterest(*interest);
              interestSent = true;
              break;
          }
      }
  }
  if (!interestSent) {
      // Fallback: if no network face is found, send via m_face (may cause a local loop)
      std::cout << "No network face found, falling back to app face " 
                << m_face->getId() << std::endl;
      m_face->sendInterest(*interest);
  }
  */
}


void
ValueProducer::OnInterest(std::shared_ptr<const ::ndn::Interest> interest) 
{
  // Get the interest name for analysis
  ::ndn::Name interestName = interest->getName();
  uint32_t appFaceId = m_face->getId();
  
  std::cout << "\nNode " << m_nodeId << " received Interest: " 
            << interestName << " via app face " << appFaceId << std::endl;
  
  // Define our local producer prefix, e.g. "/aggregate/<m_nodeId>"
  ::ndn::Name localPrefix("/aggregate");
  localPrefix.appendNumber(m_nodeId);
  
  // Remove any sequence components for a fair comparison
  ::ndn::Name nameWithoutSeq = ns3::ndn::AggregateUtils::getNameWithoutSequence(interestName);
  ::ndn::Name localPrefixWithoutSeq = ns3::ndn::AggregateUtils::getNameWithoutSequence(localPrefix);
  
  // If this interest is for our own data, process it
  if (nameWithoutSeq == localPrefixWithoutSeq) {
    std::cout << "* Node " << m_nodeId << " received direct request for its data" << std::endl;
    
    auto data = std::make_shared<::ndn::Data>(interestName);
    uint64_t val = static_cast<uint64_t>(m_nodeId);
    uint64_t netVal = htobe64(val);
    auto buffer = std::make_shared<::ndn::Buffer>(
      reinterpret_cast<const uint8_t*>(&netVal), sizeof(netVal));
    data->setContent(buffer);
    data->setFreshnessPeriod(::ndn::time::seconds(1));
    
    ns3::ndn::StackHelper::getKeyChain().sign(*data);
    
    m_transmittedDatas(data, this, m_face);
    m_face->sendData(*data);
    
    std::cout << "Node " << m_nodeId << " produced Data with value = " 
              << val << " at " << std::fixed << std::setprecision(2)
              << ns3::Simulator::Now().GetSeconds() << "s" << std::endl << std::flush;
    return;
  }
  
  // IMPORTANT CHANGE: For non-self interests, use ForwardToStrategy
  std::cout << "* Node " << m_nodeId 
            << " directly forwarding interest to NFD" << std::endl;
  
  // Debug face states and FIB entries
  DebugFibEntries("Before forwarding interest");
  ForwardToStrategy(interest);
  
  // Schedule face statistics check
  Simulator::Schedule(MilliSeconds(100), &ValueProducer::DebugFaceStats, this);
}


// Simplified OnData implementation to work with AggregateStrategy
void
ValueProducer::OnData(std::shared_ptr<const ::ndn::Data> data)
{ 
  ::ndn::Name dataName = data->getName();
  std::cout << "\nNode " << m_nodeId << " received Data: " << dataName << std::endl << std::flush;
  
  // NEW: Check if this is a response to our self-generated interest
  bool isResponseToMyInterest = false;
  if (m_prefix.size() > 0) {  // Only check if we set a prefix (meaning we're acting as consumer)
    // Compare names, ignoring sequence numbers
    Name prefix1 = ns3::ndn::AggregateUtils::getNameWithoutSequence(dataName);
    Name prefix2 = ns3::ndn::AggregateUtils::getNameWithoutSequence(m_prefix);
    
    if (prefix1 == prefix2) {
      isResponseToMyInterest = true;
      std::cout << "✓ Node " << m_nodeId << " received response to self-generated interest!" << std::endl;
      
      // Extract the aggregated value
      if (data->getContent().value_size() >= sizeof(uint64_t)) {
        uint64_t netBytes;
        std::memcpy(&netBytes, data->getContent().value(), sizeof(uint64_t));
        uint64_t aggregatedValue = be64toh(netBytes);
        
        std::cout << "❗ FINAL RESULT: Node " << m_nodeId << " received aggregated value: " 
                  << aggregatedValue << " at " << std::fixed << std::setprecision(2)
                  << ns3::Simulator::Now().GetSeconds() << "s" << std::endl;
      }
      
      // Let standard NDN processing occur for PIT cleanup
      App::OnData(data);
      return;  // Skip the rest of processing
    }
  }

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
    static std::set<std::string> processedDataNames; // Track data we've already sent
    std::string dataNameStr = dataName.toUri();
    
    // Skip if we've already processed this data packet
    if (processedDataNames.find(dataNameStr) != processedDataNames.end()) {
      std::cout << "* Node " << m_nodeId << " already processed data " << dataNameStr << " - skipping to avoid loops" << std::endl;
      return; // Skip further processing
    }
    
    // Mark this data as processed
    processedDataNames.insert(dataNameStr);
    
    std::cout << "* Node " << m_nodeId << " received self-produced data - forwarding to network" << std::endl;
    
    // Get access to L3 protocol and faces
    auto l3proto = GetNode()->GetObject<ns3::ndn::L3Protocol>();
    if (!l3proto) {
      std::cout << "ERROR: Could not get L3Protocol!" << std::endl;
      return;
    }
  
    // Find the face that connects to the network
    const nfd::FaceTable& faceTable = l3proto->getFaceTable();
    auto forwarder = l3proto->getForwarder();
    bool dataForwarded = false;
    nfd::Face* networkFace = nullptr;
    
    // Find a suitable network face
    for (const auto& face : faceTable) {
      if (face.getId() == m_face->getId()) continue; // Skip app face
      
      auto transport = face.getTransport();
      if (transport) {
        std::string uriStr = transport->getLocalUri().toString();
        if (uriStr.find("netdev://") == 0) {
          networkFace = const_cast<nfd::Face*>(&face);
          std::cout << "  Found network face " << face.getId() << " for data injection" << std::endl;
          break;
        }
      }
    }
    
    if (networkFace) {
      // Create a properly formatted & signed Data packet
      std::cout << "  Creating properly formatted Data packet for: " << data->getName() << std::endl;
      
      // The key fix: Create a fresh Data packet with the same content but ensure proper signing
      auto freshData = std::make_shared<::ndn::Data>(data->getName());
      freshData->setContent(data->getContent());
      freshData->setFreshnessPeriod(data->getFreshnessPeriod());
      
      // This is critical - use the KeyChain to properly sign the data packet
      // This is the simplest and most reliable approach
      ns3::ndn::StackHelper::getKeyChain().sign(*freshData);
      
      // Send it via the network face
      std::cout << "  Sending properly formatted Data packet via face " << networkFace->getId() << std::endl;
      networkFace->sendData(*freshData);
      
      // Record that we've processed this data
      dataForwarded = true;
    } else {
      std::cout << "  WARNING: No suitable network face found for data injection" << std::endl;
    }
    
    // Important: Don't call App::OnData, as this could re-process the data
    // App::OnData(data); - REMOVE THIS LINE
    return; // Exit immediately after forwarding
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

// Update the ForwardToStrategy method:

void
ValueProducer::ForwardToStrategy(std::shared_ptr<const ::ndn::Interest> interest)
{
  std::cout << "* Node " << m_nodeId << " DIRECT FORWARDING to strategy" << std::endl;
  
  // Get the NFD pipeline directly
  auto l3proto = GetNode()->GetObject<ns3::ndn::L3Protocol>();
  if (!l3proto) {
    std::cout << "ERROR: Could not get L3Protocol!" << std::endl;
    return;
  }
  
  auto forwarder = l3proto->getForwarder();
  if (!forwarder) {
    std::cout << "ERROR: Could not get Forwarder!" << std::endl;
    return;
  }
  
  // Create a copy of the interest with a new nonce to avoid duplicate detection
  auto newInterest = std::make_shared<::ndn::Interest>(*interest);
  newInterest->refreshNonce();
  
  // CRITICAL FIX: Create a PIT entry before forwarding
  auto pitInsertResult = forwarder->getPit().insert(*newInterest);
  std::shared_ptr<nfd::pit::Entry> pitEntry = pitInsertResult.first;
  
  // Add this app face as an IN record in the PIT entry
  pitEntry->insertOrUpdateInRecord(*m_face, *newInterest);
  
  std::cout << "  [IMPORTANT] Created PIT entry for " << newInterest->getName() 
            << " with app face " << m_face->getId() << " as InRecord" << std::endl;
  
  // Look up the FIB entry for this interest
  const auto& fib = forwarder->getFib();
  const auto& fibEntry = fib.findLongestPrefixMatch(newInterest->getName());
  
  if (fibEntry.getNextHops().empty()) {
    std::cout << "ERROR: No next hops found in FIB for " << newInterest->getName() << std::endl;
    return;
  }
  
  // Get the best next hop (the first one with lowest cost)
  bool sentInterest = false;
  for (const auto& nextHop : fibEntry.getNextHops()) {
    const nfd::Face& face = nextHop.getFace();
    
    // Skip the app face and internal faces
    if (face.getId() == m_face->getId() || face.getId() <= 1) {
      continue;
    }
    
    // Get the URI to check if it's a network face
    auto transport = face.getTransport();
    if (transport) {
      std::string uriStr = transport->getLocalUri().toString();
      
      // Only use network faces (netdev://)
      if (uriStr.find("netdev://") == 0) {
        std::cout << "  Using FIB entry: " << fibEntry.getPrefix() 
                  << " → sending via face " << face.getId() 
                  << " (cost: " << nextHop.getCost() << ")" << std::endl;
        
        // IMPORTANT: Add an OUT record to the PIT entry
        nfd::Face* mutableFace = const_cast<nfd::Face*>(&face);
        pitEntry->insertOrUpdateOutRecord(*mutableFace, *newInterest);
        
        // Send via this face
        mutableFace->sendInterest(*newInterest);
        
        // DEBUG: Verify PIT entry after sending
        std::cout << "  [PIT-VERIFY] After sending, PIT entry has " 
                  << pitEntry->getInRecords().size() << " in-records and "
                  << pitEntry->getOutRecords().size() << " out-records" << std::endl;
                
        sentInterest = true;
        break;
      }
    }
  }
  
  if (!sentInterest) {
    std::cout << "ERROR: Could not find suitable network face to send interest" << std::endl;
    std::cout << "  Available next hops for " << fibEntry.getPrefix() << ":" << std::endl;
    for (const auto& nh : fibEntry.getNextHops()) {
      std::cout << "    Face " << nh.getFace().getId() 
                << " (cost: " << nh.getCost() << ")" << std::endl;
    }
  }
  
  // Verify PIT entries after forwarding
  Simulator::Schedule(MilliSeconds(5), &ValueProducer::DebugPitState, 
                     this, newInterest->getName());
}

} // namespace ndn
} // namespace ns3