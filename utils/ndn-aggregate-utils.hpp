#ifndef NDN_AGGREGATE_UTILS_HPP
#define NDN_AGGREGATE_UTILS_HPP

#include "ns3/core-module.h"
#include "ns3/global-value.h"
#include "ns3/node-container.h"
#include "ns3/uinteger.h"
#include <string>

namespace ns3 {
namespace ndn {

/**
 * @brief Utility class for NDN aggregation functionality
 */
class AggregateUtils
{
public:
  /**
   * @brief Node role definition
   */
  enum class NodeRole {
    PRODUCER,    // P1, P2, etc.
    RACK_AGG,    // R1, R2, etc.
    CORE_AGG,    // C1, C2, etc.
    UNKNOWN
  };

  /**
   * @brief Determine the role of a node based on its index
   * @param nodeIndex the zero-based index of the node
   * @return The node's role in the topology
   */
  static NodeRole
  determineNodeRole(uint32_t nodeIndex);
  
  /**
   * @brief Get a human-readable string representing the node's role
   * @param role The node's role
   * @param nodeIndex The zero-based index of the node
   * @return String representation (e.g., "P1", "R2", "C1")
   */
  static std::string
  getNodeRoleString(NodeRole role, uint32_t nodeIndex);
  
  /**
   * @brief Get node count from GlobalValue or fallback to calculation
   * @return The number of producer nodes in the topology
   */
  static uint32_t
  getNodeCount();
};

} // namespace ndn
} // namespace ns3

#endif // NDN_AGGREGATE_UTILS_HPP