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
                                  MakeIntegerChecker<int>());
  return tid;
}

void
ValueProducer::StartApplication() 
{
  App::StartApplication();
  // Get this node's ID (set via attribute or use Node's system ID)
  if (m_nodeId == 0) {
    m_nodeId = GetNode()->GetId(); // default to NS-3 node ID if not set
  }
  
  // Register prefix using binary format (BUG FIX)
  ::ndn::Name binName("/aggregate");
  binName.appendNumber(m_nodeId); // Binary format (will create /aggregate/%01, etc.)
  std::string prefixUri = binName.toUri();
  
  FibHelper::AddRoute(GetNode(), prefixUri, m_face, 0);
  NS_LOG_INFO("Node " << m_nodeId << " registered binary prefix: " << prefixUri);
}

void
ValueProducer::OnInterest(std::shared_ptr<const ::ndn::Interest> interest) 
{
  App::OnInterest(interest); // forwarder processes Interest first
  ::ndn::Name interestName = interest->getName();
  
  // Note: We can only access our app's face (m_face), not the actual incoming network face
  uint32_t appFaceId = m_face->getId();
  
  NS_LOG_INFO("Node " << m_nodeId << " received Interest: " << interestName 
              << " via app face " << appFaceId);

  // BUG FIX: Check prefix and ID component, allow sequence numbers
  if (interestName.size() >= 2 && 
      interestName.get(0).toUri() == "aggregate") {
    
    // Try to extract and compare component 1 (the ID)
    bool isMatch = false;
    
    try {
      // This will work for binary-encoded components like %04
      uint64_t requestedId = interestName.get(1).toNumber();
      isMatch = (requestedId == static_cast<uint64_t>(m_nodeId));
      std::cout << "\nChecking if ID " << requestedId << " matches node ID " 
                << m_nodeId << ": " << (isMatch ? "yes" : "no") << std::endl;
    }
    catch (const std::exception& e) {
      // Fallback to string comparison for non-numeric components
      isMatch = (std::to_string(m_nodeId) == interestName.get(1).toUri());
    }
    
    if (isMatch) {
      // Create Data packet with name equal to Interest's name
      auto data = std::make_shared<::ndn::Data>(interestName);
      // Content: 8-byte integer equal to m_nodeId
      uint64_t val = (uint64_t) m_nodeId;
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
            << " with value = " << m_nodeId 
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
  App::OnData(data); // Important: call parent implementation
  
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

} // namespace ndn
} // namespace ns3