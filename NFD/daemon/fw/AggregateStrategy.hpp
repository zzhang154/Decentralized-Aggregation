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
  virtual void afterReceiveInterest(const ndn::Interest& interest, const FaceEndpoint& ingress,
                                    const std::shared_ptr<pit::Entry>& pitEntry) override;
  virtual void afterReceiveData(const ndn::Data& data, const FaceEndpoint& ingress,
                                const std::shared_ptr<pit::Entry>& pitEntry) override;
  virtual void beforeSatisfyInterest(const Data& data, const FaceEndpoint& ingress,
                                     const std::shared_ptr<pit::Entry>& pitEntry) override;
  void beforeExpirePendingInterest(const std::shared_ptr<pit::Entry>& pitEntry);
  void processDataForAggregation(const Data& data, const FaceEndpoint& ingress,
                                 const std::shared_ptr<pit::Entry>& pitEntry);

private:
  // Store our own reference to the Forwarder
  Forwarder& m_forwarder;
  uint32_t m_nodeId;
  ns3::ndn::AggregateUtils::NodeRole m_nodeRole;
  int m_logicalId;  // 1-based ID within role group

  void registerPitExpirationCallback();

  void processSubInterestData(const Data& data, const Name& dataName,
                              const FaceEndpoint& ingress,
                              const std::shared_ptr<pit::Entry>& pitEntry);
  void processWaitingInterestData(const Data& data, const Name& dataName,
                                  const FaceEndpoint& ingress,
                                  const std::shared_ptr<pit::Entry>& pitEntry);
  void processDirectData(const Data& data, const Name& dataName,
                         const FaceEndpoint& ingress,
                         const std::shared_ptr<pit::Entry>& pitEntry);

  // Define WaitInfo structure to track waiting dependencies
  struct WaitInfo {
    // Maps ID -> Name of Interest that will provide this ID's data
    std::unordered_map<int, ndn::Name> waitingFor;
  };

  // Structure to hold strategy-specific info for each PIT entry
  struct AggregatePitInfo : public StrategyInfo {
    static constexpr int getTypeId() {
      return 1000; // unique ID for this custom strategy info
    }

    std::set<int> neededIds;
    std::set<int> pendingIds;
    uint64_t partialSum;
    std::vector<std::weak_ptr<pit::Entry>> dependentInterests;
    std::shared_ptr<WaitInfo> waitInfo;
  };

  struct AggregateSubInfo : public StrategyInfo {
    static constexpr int getTypeId() {
      return 1001; // unique ID different from AggregatePitInfo
    }
    std::shared_ptr<pit::Entry> parentEntry;
  };

  // Helper to retrieve (and create if not exists) the AggregatePitInfo for a PIT entry
  AggregatePitInfo* getAggregatePitInfo(const std::shared_ptr<pit::Entry>& pitEntry);
  // Helper for Producer Interest Handling
  bool isSelfGeneratedInterest(const std::set<int>& requestedIds);
  bool isDirectDataRequest(const std::set<int>& requestedIds);

  // Debug helper functions for afterReceiveInterest
  void logDebugInfo(const ndn::Interest& interest, const FaceEndpoint& ingress);
  bool checkInterestAggregation(const ndn::Interest& interest, const FaceEndpoint& ingress,
                                const std::shared_ptr<pit::Entry>& pitEntry);
  void forwardRegularInterest(const ndn::Interest& interest, const FaceEndpoint& ingress,
                              const std::shared_ptr<pit::Entry>& pitEntry);
  bool processContentStoreHits(const ndn::Interest& interest, const FaceEndpoint& ingress,
                               const std::shared_ptr<pit::Entry>& pitEntry, AggregatePitInfo* pitInfo);
  void checkSubsetSupersetRelationships(const ndn::Interest& interest, const std::shared_ptr<pit::Entry>& pitEntry,
                                        AggregatePitInfo* pitInfo, const std::set<int>& requestedIds);
  void splitAndForwardInterests(const ndn::Interest& interest, const FaceEndpoint& ingress,
                                const std::shared_ptr<pit::Entry>& pitEntry, AggregatePitInfo* pitInfo);
  void handleSingleFaceForwarding(const ndn::Interest& interest, const FaceEndpoint& ingress,
                                  const std::shared_ptr<pit::Entry>& pitEntry,
                                  AggregatePitInfo* pitInfo,
                                  const std::map<Face*, std::vector<int>>& faceToIdsMap);
  void printPitDebugInfo(const Pit& pit);

  // Helper functions for beforeSatisfyInterest
  void cleanupSatisfiedPitEntries();

  // Helper functions for processing sub-interest Data
  std::pair<std::shared_ptr<pit::Entry>, AggregatePitInfo*> findParentPitEntry(const Name& dataName);
  uint64_t updateParentWithSubInterestData(const ndn::Data& data, const Name& dataName, AggregatePitInfo* parentInfo);
  void sendAggregatedDataToParentFaces(std::shared_ptr<pit::Entry> parentPit, AggregatePitInfo* parentInfo);
  void satisfyPiggybackedInterests(AggregatePitInfo* parentInfo);
  std::vector<Face*> extractFacesFromPitEntry(const std::shared_ptr<pit::Entry>& pitEntry);
  void sendDataDirectly(const std::shared_ptr<ndn::Data>& data, Face* outFace,
                        const Name& dataName, uint64_t value);

  // ** Data structures for coordinating sub-Interests and piggybacking **
  std::map<Name, std::weak_ptr<pit::Entry>> m_parentMap;
  std::map<Name, std::vector<std::weak_ptr<pit::Entry>>> m_waitingInterests;
  static std::unordered_map<int, uint64_t> m_cachedValues;
};

} // namespace fw
} // namespace nfd

#endif // AGGREGATE_STRATEGY_HPP
