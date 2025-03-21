#include "ndn-aggregate-utils.hpp"

namespace ns3 {
namespace ndn {

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

} // namespace ndn
} // namespace ns3