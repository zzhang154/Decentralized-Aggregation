// AggregateStrategy.hpp
#ifndef AGGREGATE_STRATEGY_HPP
#define AGGREGATE_STRATEGY_HPP

#include "ns3/ndnSIM/NFD/daemon/fw/strategy.hpp"
#include "ns3/ndnSIM/NFD/daemon/fw/forwarder.hpp"
#include "ns3/ndnSIM/NFD/daemon/table/pit-entry.hpp"
#include "ns3/ndnSIM/NFD/daemon/table/cs.hpp"
#include "ns3/ndnSIM/NFD/daemon/face/face-endpoint.hpp"
#include "ns3/ndnSIM/model/ndn-common.hpp"
#include <set>
#include <vector>
#include <stdint.h>
#include <iostream>

namespace nfd {
namespace fw {

class AggregateStrategy : public Strategy {
public:
  // Register the strategy with a unique name so it can be used in StrategyChoiceHelper
  AggregateStrategy(Forwarder& forwarder, const Name& name = getStrategyName());

  static const Name& getStrategyName();  // returns "/localhost/nfd/strategy/aggregate"

  // ** Strategy callback overrides **
  // Change in AggregateStrategy.hpp:
  virtual void
  afterReceiveInterest(const ndn::Interest& interest, const nfd::FaceEndpoint& ingress,
                      const std::shared_ptr<nfd::pit::Entry>& pitEntry) override;

  virtual void
  afterReceiveData(const ndn::Data& data, const nfd::FaceEndpoint& ingress,
                  const std::shared_ptr<nfd::pit::Entry>& pitEntry) override;

  void 
  beforeExpirePendingInterest(const shared_ptr<pit::Entry>& pitEntry);

private:
  // Store our own reference to the Forwarder
  Forwarder& m_forwarder;  // <-- The KEY fix
  uint32_t m_nodeId;
  
  // Node role mapping
  enum class NodeRole {
    PRODUCER,    // P1, P2, etc.
    RACK_AGG,    // R1, R2, etc. 
    CORE_AGG,    // C1, C2, etc.
    UNKNOWN
  };
  
  NodeRole m_nodeRole;
  int m_logicalId;  // 1-based ID within role group
  
  void determineNodeRole();
  std::string getNodeRoleString() const;

  // Add this method to register for PIT expiration events
  void registerPitExpirationCallback();

  // Structure to hold strategy-specific info for each PIT entry (interest)
  struct AggregatePitInfo : public StrategyInfo {
    // (BUG FIX) Add this static method that returns a unique ID for this strategy info type
    static constexpr int
    getTypeId() {
      return 1000; // Choose a unique ID (typically 1000+ for custom strategies)
    }

    std::set<int>    neededIds;        // full set of IDs this Interest is requesting
    std::set<int>    pendingIds;       // IDs still pending (not yet received or satisfied)
    uint64_t         partialSum;       // sum of all received components so far
    std::vector<std::weak_ptr<pit::Entry>> dependentInterests; // piggybacked interests waiting on this one
  };

  // Add this after the AggregatePitInfo struct definition (around line 50)
  struct AggregateSubInfo : public StrategyInfo {
    static constexpr int
    getTypeId() {
      return 1001; // Choose a unique ID different from AggregatePitInfo
    }

    // Explicitly store the parent PIT entry
    shared_ptr<pit::Entry> parentEntry;
  };

  // Helper to parse an aggregate Interest Name into a set of integer IDs
  std::set<int> parseRequestedIds(const Interest& interest) const;

  // Helper to retrieve (and create if not exists) the AggregatePitInfo for a PIT entry
  AggregatePitInfo* getAggregatePitInfo(const std::shared_ptr<pit::Entry>& pitEntry);

  /**
   * @brief Special handling for consumer-producer nodes receiving interests
   *
   * Handles dual role: responds to interests for own data, forwards other interests to rack aggregator
   */
  void 
  handleProducerInterest(const Interest& interest, const FaceEndpoint& ingress,
                         const shared_ptr<pit::Entry>& pitEntry);
                         
  /**
   * @brief Special handling for consumer-producer nodes receiving data
   *
   * Handles dual role: processes incoming aggregated results, ensures proper data forwarding
   */
  void
  handleProducerData(const Data& data, const FaceEndpoint& ingress,
                     const shared_ptr<pit::Entry>& pitEntry);

  // ** Data structures for coordinating sub-Interests and piggybacking **

  // Map from a sub-interest Name (subset of IDs) to the parent PIT entry that initiated it
  // (Used to aggregate Data back into the parent interest’s result)
  std::map<Name, std::weak_ptr<pit::Entry>> m_parentMap;

  // Map to track piggyback relationships: Data name -> list of PIT entries waiting for this Data (when an interest piggybacks on another interest’s retrieval)
  std::map<Name, std::vector<std::weak_ptr<pit::Entry>>> m_waitingInterests;

  // A simple cache for atomic values (Data for single IDs) to leverage Content Store hits
  // Key: ID, Value: last known data value (assuming 64-bit content)
  static std::unordered_map<int, uint64_t> m_cachedValues;
};

} // namespace fw
} // namespace nfd

#endif // AGGREGATE_STRATEGY_HPP
