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
#include <unordered_map>

#include "ns3/ndnSIM/utils/ndn-aggregate-utils.hpp"

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
  processDataForAggregation(const Data& data, const FaceEndpoint& ingress,
                            const shared_ptr<pit::Entry>& pitEntry);

  void 
  beforeExpirePendingInterest(const shared_ptr<pit::Entry>& pitEntry);

  // Fix the declaration in the header:
  virtual void
  beforeSatisfyInterest(const Data& data,
                      const FaceEndpoint& ingress, 
                      const shared_ptr<pit::Entry>& pitEntry) override;

private:
  // Store our own reference to the Forwarder
  Forwarder& m_forwarder;  // <-- The KEY fix
  uint32_t m_nodeId;
  
  
  ns3::ndn::AggregateUtils::NodeRole m_nodeRole;
  int m_logicalId;  // 1-based ID within role group
  
  // Add this method to register for PIT expiration events
  void registerPitExpirationCallback();

  // New method to process sub-interest data
  void
  processSubInterestData(const Data& data, const Name& dataName, 
                        const FaceEndpoint& ingress,
                        const shared_ptr<pit::Entry>& pitEntry);
                        
  // New method to process waiting interests
  void
  processWaitingInterestData(const Data& data, const Name& dataName, 
                           const FaceEndpoint& ingress,
                           const shared_ptr<pit::Entry>& pitEntry);
                           
  // New method to process direct data
  void
  processDirectData(const Data& data, const Name& dataName, 
                   const FaceEndpoint& ingress,
                   const shared_ptr<pit::Entry>& pitEntry);

  // Define WaitInfo structure to track waiting dependencies
  struct WaitInfo {
    // Maps ID -> Name of Interest that will provide this ID's data
    std::unordered_map<int, ndn::Name> waitingFor;
  };  

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
    std::shared_ptr<WaitInfo> waitInfo;  // For tracking where data for each ID will come from
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

  // Helper to retrieve (and create if not exists) the AggregatePitInfo for a PIT entry
  AggregatePitInfo* getAggregatePitInfo(const std::shared_ptr<pit::Entry>& pitEntry);

  // Debug helper functions for "afterReceiveInterest"
  void logDebugInfo(const Interest& interest, const FaceEndpoint& ingress);
  bool checkInterestAggregation(const Interest& interest, const FaceEndpoint& ingress, 
                              const shared_ptr<pit::Entry>& pitEntry);
  void forwardRegularInterest(const Interest& interest, const FaceEndpoint& ingress,
                            const shared_ptr<pit::Entry>& pitEntry);
  bool processContentStoreHits(const Interest& interest, const FaceEndpoint& ingress,
                              const shared_ptr<pit::Entry>& pitEntry, 
                              AggregatePitInfo* pitInfo);
  void checkSubsetSupersetRelationships(const Interest& interest, 
                                      const shared_ptr<pit::Entry>& pitEntry,
                                      AggregatePitInfo* pitInfo,
                                      const std::set<int>& requestedIds);
  void splitAndForwardInterests(const Interest& interest, const FaceEndpoint& ingress,
                              const shared_ptr<pit::Entry>& pitEntry,
                              AggregatePitInfo* pitInfo);
  void handleSingleFaceForwarding(const Interest& interest, const FaceEndpoint& ingress,
                                const shared_ptr<pit::Entry>& pitEntry,
                                AggregatePitInfo* pitInfo,
                                const std::map<Face*, std::vector<int>>& faceToIdsMap);
  void forwardSplitInterests(const Interest& interest, const FaceEndpoint& ingress,
                          const shared_ptr<pit::Entry>& pitEntry,
                          const std::map<Face*, std::vector<int>>& faceToIdsMap);
  void printPitDebugInfo(const Pit& pit);

  // Debug helper functions for "processSubInterestData"
  /**
   * @brief Find and validate the parent PIT entry for a sub-interest response
   * @param dataName The name of the data packet received
   * @return Shared pointer to parent PIT entry and its info, or nullptrs if invalid
   */
  std::pair<std::shared_ptr<pit::Entry>, AggregatePitInfo*> 
  findParentPitEntry(const Name& dataName);

  /**
   * @brief Update parent's partial sum and pending IDs with sub-interest data
   * @param data The data packet received
   * @param dataName The name of the data packet
   * @param parentInfo The parent PIT entry's strategy info
   * @return Total sum value from data
   */
  uint64_t updateParentWithSubInterestData(const Data& data, const Name& dataName, 
                                        AggregatePitInfo* parentInfo);

  /**
   * @brief Send aggregated data to parent's faces when all components are received
   * @param parentPit The parent PIT entry
   * @param parentInfo The parent's strategy info
   */
  void sendAggregatedDataToParentFaces(std::shared_ptr<pit::Entry> parentPit,
                                      AggregatePitInfo* parentInfo);

  /**
   * @brief Process and satisfy any piggybacked interests
   * @param parentInfo The parent PIT entry's strategy info
   */
  void satisfyPiggybackedInterests(AggregatePitInfo* parentInfo);

  /**
   * @brief Extract faces from a PIT entry for sending data
   * @param pitEntry The PIT entry to extract faces from
   * @return Vector of faces extracted from the PIT entry
   */
  std::vector<Face*> extractFacesFromPitEntry(const std::shared_ptr<pit::Entry>& pitEntry);

  /**
   * @brief Send data directly to a face, bypassing PIT
   * @param data The data packet to send
   * @param outFace The face to send the data to
   * @param dataName The name of the data (for logging)
   * @param value The value contained in the data (for logging)
   */
  void sendDataDirectly(const std::shared_ptr<::ndn::Data>& data, Face* outFace, 
                      const Name& dataName, uint64_t value);

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
