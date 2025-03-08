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
  // Register prefix for this node's data (so interest /aggregate/<nodeId> will reach here)
  std::string prefix = "/aggregate/" + std::to_string(m_nodeId);
  FibHelper::AddRoute(GetNode(), prefix, m_face, 0);
}

void
ValueProducer::OnInterest(std::shared_ptr<const ::ndn::Interest> interest) 
{
  App::OnInterest(interest); // forwarder processes Interest first
  ::ndn::Name interestName = interest->getName();
  NS_LOG_INFO("Node " << m_nodeId << " received Interest: " << interest->getName());
  // If the Interest exactly matches this node's prefix (single ID)
  if (interestName.size() == 2 && std::to_string(m_nodeId) == interestName.get(1).toUri()) {
    // Create Data packet with name equal to Interest's name
    auto data = std::make_shared<::ndn::Data>(interestName);
    // Content: 8-byte integer equal to m_nodeId (for consistency we use 64-bit)
    uint64_t val = (uint64_t) m_nodeId;
    uint64_t netVal = htobe64(val);
    
    // Create a buffer for the content
    std::shared_ptr<::ndn::Buffer> buffer = std::make_shared<::ndn::Buffer>(
      reinterpret_cast<const uint8_t*>(&netVal), sizeof(netVal));
    data->setContent(buffer);
    data->setFreshnessPeriod(::ndn::time::seconds(1));  // 1 second freshness
    
    // Sign the data
    ns3::ndn::StackHelper::getKeyChain().sign(*data);
    
    // Send the Data packet
    m_transmittedDatas(data, this, m_face);
    m_face->sendData(*data);
    NS_LOG_INFO("Node " << m_nodeId << " produced Data for " << interestName.toUri()
                << " with value = " << m_nodeId);
  }
}

} // namespace ndn
} // namespace ns3