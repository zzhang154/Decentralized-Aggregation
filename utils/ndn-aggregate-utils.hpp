#ifndef NDN_AGGREGATE_UTILS_HPP
#define NDN_AGGREGATE_UTILS_HPP

#include "ns3/core-module.h"
#include "ns3/global-value.h"
#include "ns3/node-container.h"
#include "ns3/uinteger.h"
#include <string>

#include <ndn-cxx/data.hpp>
#include <ndn-cxx/name.hpp>
#include <set>

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
  static NodeRole determineNodeRole(uint32_t nodeIndex);
  
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

  // Add these new utility functions:

  /**
   * @brief Extract a numeric value from Data content
   * @param data The NDN data packet
   * @return The numeric value extracted from content
   */
  static uint64_t extractValueFromContent(const ::ndn::Data& data);

  /**
   * @brief Parse numbers from an NDN name (components that can be converted to integers)
   * @param name The NDN name to parse
   * @return Set of integers found in the name
   */
  static std::set<int> parseNumbersFromName(const ::ndn::Name& name);
  
  /**
   * @brief Create an NDN data packet with a numeric value as content
   * @param name The name for the data packet
   * @param value The numeric value to include
   * @return Shared pointer to the created Data object
   */
  static std::shared_ptr<::ndn::Data> createDataWithValue(const ::ndn::Name& name, uint64_t value);

  /**
   * @brief Check if a name is for an aggregation interest/data
   * @param name The NDN name to check
   * @return True if the name is for aggregation
   */
  static bool isAggregationName(const ::ndn::Name& name);

  /**
   * @brief Extract logical IDs from an aggregate name (skip sequence numbers)
   * @param name The aggregate name
   * @return Set of IDs contained in the name
   */
  static std::set<int> extractIdsFromName(const ::ndn::Name& name);

  /**
   * @brief Sign a data packet using the NDN keychain
   * @param data The data packet to sign
   */
  static void signData(std::shared_ptr<::ndn::Data> data);
};

} // namespace ndn
} // namespace ns3

#endif // NDN_AGGREGATE_UTILS_HPP