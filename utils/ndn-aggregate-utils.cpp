#include "ndn-aggregate-utils.hpp"

#include <ndn-cxx/data.hpp>
#include <ndn-cxx/name.hpp>
#include <ndn-cxx/encoding/block.hpp>
#include <endian.h>
#include "ns3/ndnSIM/helper/ndn-stack-helper.hpp"
#include <iomanip>

namespace ns3 {
namespace ndn {

// Implement existing functions (getNodeCount, determineNodeRole, getNodeRoleString)
// ...

uint32_t
AggregateUtils::getNodeCount()
{
  uint32_t nodeCount = 0;
  ns3::UintegerValue val;
  bool exists = ns3::GlobalValue::GetValueByNameFailSafe("NodeCount", val);
  
  if (exists) {
    nodeCount = val.Get();
  } else {
    // Fallback if not found
    nodeCount = std::max(2u, NodeContainer::GetGlobal().GetN() / 3);
  }
  
  return nodeCount;
}

AggregateUtils::NodeRole
AggregateUtils::determineNodeRole(uint32_t nodeIndex)
{
  // Get nodeCount from GlobalValue
  uint32_t nodeCount = getNodeCount();
  
  // Calculate topology elements - match exactly with createTopology()
  uint32_t numRackAggregators = nodeCount;  // One rack aggregator per producer
  uint32_t numCoreAggregators = (nodeCount > 1) ? std::max(1u, nodeCount / 4) : 0;
  
  // Determine role based on index ranges
  if (nodeIndex < nodeCount) {
    // First nodeCount nodes are producers
    return NodeRole::PRODUCER;
  }
  else if (nodeIndex < nodeCount + numRackAggregators) {
    // Next numRackAggregators nodes are rack aggregators
    return NodeRole::RACK_AGG;
  }
  else {
    // Remaining nodes are core aggregators
    return NodeRole::CORE_AGG;
  }
}

std::string
AggregateUtils::getNodeRoleString(NodeRole role, uint32_t nodeIndex)
{
  uint32_t nodeCount = getNodeCount();
  uint32_t numRackAggregators = nodeCount;
  
  // Calculate logical ID (1-based)
  int logicalId = 0;
  switch (role) {
    case NodeRole::PRODUCER:
      logicalId = nodeIndex + 1;
      return "P" + std::to_string(logicalId);
    case NodeRole::RACK_AGG:
      logicalId = nodeIndex - nodeCount + 1;
      return "R" + std::to_string(logicalId);
    case NodeRole::CORE_AGG:
      logicalId = nodeIndex - (nodeCount + numRackAggregators) + 1;
      return "C" + std::to_string(logicalId);
    default:
      return "NODE " + std::to_string(nodeIndex + 1);
  }
}

// Implement the NDN specific utility functions
uint64_t
AggregateUtils::extractValueFromContent(const ::ndn::Data& data)
{
  uint64_t value = 0;
  // Assume content is exactly an 8-byte integer in network byte order
  if (data.getContent().value_size() >= sizeof(uint64_t)) {
    uint64_t netBytes;
    std::memcpy(&netBytes, data.getContent().value(), sizeof(uint64_t));
    value = be64toh(netBytes);
  } else {
    // If content is not 8 bytes, we try to interpret it as text
    std::string text(reinterpret_cast<const char*>(data.getContent().value()),
                     data.getContent().value_size());
    try {
      value = std::stoull(text);
    } catch (...) {
      value = 0;
    }
  }
  return value;
}

std::set<int>
AggregateUtils::parseNumbersFromName(const ::ndn::Name& name)
{
  std::set<int> idSet;
  
  // Skip the first component (typically "aggregate") and skip sequence number
  for (size_t i = 1; i < name.size(); ++i) {
    // Skip sequence number components (they have a specific format)
    std::string uri = name[i].toUri();
    if (uri.find("seq=") != std::string::npos) {
      continue;
    }
    
    try {
      // Remove any % prefix (used for NDN component encoding)
      if (uri[0] == '%') {
        uri = uri.substr(1);
      }
      
      int id = std::stoi(uri);
      if (id > 0) {
        idSet.insert(id);
      }
    } 
    catch (const std::exception& e) {
      // Skip components that can't be parsed as integers
    }
  }
  
  return idSet;
}

std::shared_ptr<::ndn::Data>
AggregateUtils::createDataWithValue(const ::ndn::Name& name, uint64_t value)
{
  auto data = std::make_shared<::ndn::Data>(name);
  
  // Convert value to network byte order
  uint64_t networkValue = htobe64(value);
  
  // Create a buffer containing the value
  std::shared_ptr<::ndn::Buffer> buffer = std::make_shared<::ndn::Buffer>(
    reinterpret_cast<const uint8_t*>(&networkValue), sizeof(networkValue));
  
  // Set as content
  data->setContent(buffer);
  
  // Set freshness period (1 second)
  data->setFreshnessPeriod(::ndn::time::milliseconds(1000));
  
  // Sign the data
  signData(data);
  
  return data;
}

bool
AggregateUtils::isAggregationName(const ::ndn::Name& name)
{
  // Check if the first component is "aggregate"
  if (name.size() > 0 && name.get(0).toUri() == "aggregate") {
    return true;
  }
  return false;
}

std::set<int>
AggregateUtils::extractIdsFromName(const ::ndn::Name& name)
{
  if (!isAggregationName(name)) {
    return {};
  }
  
  return parseNumbersFromName(name);
}

void
AggregateUtils::signData(std::shared_ptr<::ndn::Data> data)
{
  // Use the keychain from StackHelper to sign the data
  ns3::ndn::StackHelper::getKeyChain().sign(*data);
}

// New utility functions - without NFD dependencies
std::shared_ptr<::ndn::Interest>
AggregateUtils::createSplitInterest(const ::ndn::Name& subInterestName,
                                  ::ndn::time::milliseconds lifetime)
{
  auto interest = std::make_shared<::ndn::Interest>(subInterestName);
  interest->setCanBePrefix(false);
  interest->setInterestLifetime(lifetime);
  return interest;
}

::ndn::Name::Component
AggregateUtils::extractSequenceComponent(const ::ndn::Name& name)
{
  for (size_t i = 0; i < name.size(); i++) {
    // Check for string-based sequence
    if (name[i].toUri().find("seq=") != std::string::npos) {
      return name[i];
    }
    // Check for native sequence numbers
    if (name[i].isSequenceNumber()) {
      return name[i];
    }
  }
  return ::ndn::Name::Component();
}

::ndn::Name
AggregateUtils::getNameWithoutSequence(const ::ndn::Name& name)
{
  ::ndn::Name result;
  
  for (size_t i = 0; i < name.size(); i++) {
    // Check for both string-based sequence markers AND native sequence numbers
    std::string uri = name[i].toUri();
    if (uri.find("seq=") == std::string::npos && !name[i].isSequenceNumber()) {
      result.append(name[i]);
    }
  }
  
  return result;
}

bool
AggregateUtils::doSequenceComponentsMatch(const ::ndn::Name& name1, const ::ndn::Name& name2)
{
  ::ndn::Name::Component seq1 = extractSequenceComponent(name1);
  ::ndn::Name::Component seq2 = extractSequenceComponent(name2);
  
  return (seq1 == seq2) || (seq1.empty() && seq2.empty());
}

bool
AggregateUtils::isSubset(const std::set<int>& potentialSubset, const std::set<int>& potentialSuperset)
{
  return std::includes(potentialSuperset.begin(), potentialSuperset.end(),
                     potentialSubset.begin(), potentialSubset.end());
}

bool
AggregateUtils::isSuperset(const std::set<int>& potentialSuperset, const std::set<int>& potentialSubset)
{
  return isSubset(potentialSubset, potentialSuperset);
}

void
AggregateUtils::logInterestInfo(const ::ndn::Interest& interest, uint32_t faceId, 
                              const std::string& nodeInfo)
{
  std::cout << '\n' << nodeInfo
          << " - STRATEGY received Interest: " << interest.getName() 
          << " via " << faceId
          << " at " << std::fixed << std::setprecision(2) << ns3::Simulator::Now().GetSeconds() 
          << "s" << std::endl << std::flush;
}

} // namespace ndn
} // namespace ns3