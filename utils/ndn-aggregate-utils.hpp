#ifndef NDN_AGGREGATE_UTILS_HPP
#define NDN_AGGREGATE_UTILS_HPP

#include "ns3/core-module.h"
#include "ns3/global-value.h"
#include "ns3/node-container.h"
#include "ns3/uinteger.h"
#include <string>

#include <ndn-cxx/interest.hpp>
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

  /**
   * @brief Create a split interest for multiple IDs
   * @param subInterestName Name for the interest
   * @param lifetime Interest lifetime
   * @return Shared pointer to the created Interest
   */
  static std::shared_ptr<::ndn::Interest> createSplitInterest(
    const ::ndn::Name& subInterestName,
    ::ndn::time::milliseconds lifetime);

  /**
   * @brief Extract sequence component from an NDN name
   * @param name The name to extract from
   * @return The sequence component, if found
   */
  static ::ndn::Name::Component extractSequenceComponent(const ::ndn::Name& name);

  /**
   * @brief Check if two sequence components match (for Interest aggregation)
   * @param name1 First name to check
   * @param name2 Second name to check
   * @return True if sequence components match
   */
  static bool doSequenceComponentsMatch(const ::ndn::Name& name1, const ::ndn::Name& name2);

  /**
   * @brief Check if a set is a subset of another
   * @param potentialSubset Set that might be a subset
   * @param potentialSuperset Set that might contain the other set
   * @return True if potentialSubset is contained in potentialSuperset
   */
  static bool isSubset(const std::set<int>& potentialSubset, const std::set<int>& potentialSuperset);

  /**
   * @brief Check if a set is a superset of another
   * @param potentialSuperset Set that might contain the other set
   * @param potentialSubset Set that might be a subset
   * @return True if potentialSuperset contains potentialSubset
   */
  static bool isSuperset(const std::set<int>& potentialSuperset, const std::set<int>& potentialSubset);

  /**
   * @brief Format and log details about an Interest
   * @param interest The interest to log
   * @param faceId The face the interest was received on
   * @param nodeInfo Node information for logging
   */
  static void logInterestInfo(const ::ndn::Interest& interest, uint32_t faceId, 
                            const std::string& nodeInfo);
};

} // namespace ndn
} // namespace ns3

#endif // NDN_AGGREGATE_UTILS_HPP