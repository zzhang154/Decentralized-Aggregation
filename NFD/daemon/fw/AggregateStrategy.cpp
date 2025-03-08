// AggregateStrategy.cpp
#include "AggregateStrategy.hpp"
#include "ns3/ndnSIM/NFD/daemon/table/fib.hpp"
#include <ndn-cxx/data.hpp>
NS_LOG_COMPONENT_DEFINE("ndn.AggregateStrategy");

namespace nfd {
namespace fw {

// Initialize static members
std::unordered_map<int, uint64_t> AggregateStrategy::m_cachedValues;

const Name& 
AggregateStrategy::getStrategyName() {
    static Name strategyName;
    static bool initialized = false;
    
    if (!initialized) {
      strategyName = Name("/localhost/nfd/strategy/aggregate");
      strategyName.appendVersion(1);  // Append version 1
      initialized = true;
    }
    
    return strategyName;
  }

// Register the strategy with ndnSIM
NFD_REGISTER_STRATEGY(AggregateStrategy);

AggregateStrategy::AggregateStrategy(Forwarder& forwarder, const Name& name)
: Strategy(forwarder)
  , m_forwarder(forwarder) // <-- The KEY fix: store forwarder reference
{
  // Set the instance name explicitly
  this->setInstanceName(name);
  
  // Logging the strategy initialization (for debugging)
  std::cout << "AggregateStrategy initialized for Forwarder." << std::endl;
}

// Helper: parse interest name of form /aggregate/<id1>/<id2>/... into a set of integers
std::set<int> 
AggregateStrategy::parseRequestedIds(const Interest& interest) const {
  std::set<int> idSet;
  const Name& name = interest.getName();
  // We assume name[0] = "aggregate", subsequent components are IDs
  for (size_t i = 1; i < name.size(); ++i) {
    try {
      int id = std::stoi(name[i].toUri()); // parse component as integer
      idSet.insert(id);
    } catch (const std::exception& e) {
      // If parsing fails, skip component
      continue;
    }
  }
  return idSet;
}

// Helper: get or create AggregatePitInfo for a pit entry
AggregateStrategy::AggregatePitInfo* 
AggregateStrategy::getAggregatePitInfo(const std::shared_ptr<pit::Entry>& pitEntry) {
  auto infoPair = pitEntry->insertStrategyInfo<AggregatePitInfo>();
  AggregatePitInfo* info = static_cast<AggregatePitInfo*>(infoPair.first);
  if (infoPair.second) {
    // Newly inserted info, initialize fields
    info->partialSum = 0;
  }
  return info;
}

// ** Main logic for processing incoming Interests **
void 
AggregateStrategy::afterReceiveInterest(const Interest& interest, const FaceEndpoint& ingress,
                                        const shared_ptr<pit::Entry>& pitEntry) 
{
  // Only operate on aggregate Interests (prefix "/aggregate")
  // (StrategyChoice should ensure this strategy only sees those by configuration)
  Name interestName = interest.getName();
  if (interestName.size() < 2 || interestName.get(0).toUri() != "aggregate") {
    // Not an aggregate interest, use default behavior (fallback to BestRoute)
    // Get the FIB entry
    const fib::Entry& fibEntry = this->lookupFib(*pitEntry);
    
    // Find a face to forward to (first available nexthop)
    const fib::NextHopList& nexthops = fibEntry.getNextHops();
    if (!nexthops.empty()) {
      Face& outFace = nexthops.begin()->getFace();
      this->sendInterest(interest, outFace, pitEntry);
    }
    return;
  }

  // Parse the set of requested data source IDs from the Interest Name
  std::set<int> requestedIds = parseRequestedIds(interest);

  // Attach strategy-specific info to the PIT entry
  AggregatePitInfo* pitInfo = getAggregatePitInfo(pitEntry);
  pitInfo->neededIds   = requestedIds;
  pitInfo->pendingIds  = requestedIds;
  pitInfo->partialSum  = 0;
  pitInfo->dependentInterests.clear(); // no piggyback dependents yet

  std::cout << ">> Received Interest " << interestName.toUri()
            << " from face " << ingress.face.getId() 
            << " requesting IDs = { ";
  for (int id : requestedIds) std::cout << id << " ";
  std::cout << "}" << std::endl;

  // ** 1. Check Content Store for cached Data that can satisfy or partially satisfy the Interest **

  // If the entire aggregated result is already cached (exact name match), the forwarder 
  // would normally handle it as a content store hit before calling strategy.
  // Here, we also check our simple cache for individual data values to avoid redundant fetches:
  for (auto it = pitInfo->pendingIds.begin(); it != pitInfo->pendingIds.end();) {
    int id = *it;
    // If cached value for this id exists, use it
    if (m_cachedValues.find(id) != m_cachedValues.end()) {
      uint64_t cachedValue = m_cachedValues[id];
      pitInfo->partialSum += cachedValue; // add to partial sum
      std::cout << "  [CacheHit] Value for ID " << id << " = " 
                << cachedValue << " (from CS)" << std::endl;
      // Mark this ID as satisfied from cache (remove from pending)
      it = pitInfo->pendingIds.erase(it);
      continue;
    }
    ++it;
  }

  // If after cache lookup, no pending IDs remain, we can satisfy the Interest immediately
  if (pitInfo->pendingIds.empty()) {
    // Create Data packet with the aggregated sum (since all components were cached)
    uint64_t totalSum = pitInfo->partialSum;
    auto data = std::make_shared<ndn::Data>(interestName);
    // Set content to the computed sum (8 bytes, network order)
    uint64_t sumNbo = htobe64(totalSum);  // convert to network byte order 64-bit
    // With one of these alternatives:
    // Option 1:
    std::shared_ptr<ndn::Buffer> buffer = std::make_shared<ndn::Buffer>(reinterpret_cast<const uint8_t*>(&sumNbo), sizeof(sumNbo));
    data->setContent(buffer);
    data->setFreshnessPeriod(::ndn::time::milliseconds(1000)); // e.g., 1 second freshness

    // Pass the incoming face as egress (using const_cast)
    // WITH THIS CORRECT VERSION FOR INTEREST SUPPRESSION:
    for (const auto& inRecord : pitEntry->getInRecords()) {
        Face& outFace = inRecord.getFace();
        this->sendData(*data, outFace, pitEntry);
    }
    std::cout << "<< Satisfied Interest " << interestName.toUri() 
              << " from cache with sum = " << totalSum << std::endl;
    return;
  }

  // ** 2. Check for Pending Interests that can be aggregated (superset or subset logic) **

  // Iterate over all PIT entries with the same prefix/name (including the current one)
  // (We use the Forwarder's PIT table to search existing entries)

  Pit& pitTable = m_forwarder.getPit();  // (BUG FIX) obtains reference to the PIT
  for (auto it = pitTable.begin(); it != pitTable.end(); ++it) {
    // each iterator element is a pit::Iterator, so dereference (*) gives you a shared_ptr<pit::Entry>
    // (BUG FIX) here, we must use a reference to avoid copying the shared_ptr.
    const pit::Entry& entryRef = *it;

    // Only consider other Interests in the aggregate namespace
    Name existingName = entryRef.getName();
    if (existingName.size() < 2 || existingName.get(0).toUri() != "aggregate")
      continue;

    // (BUG FIX) Compare pointer addresses to see if it's the same PIT entry
    if (&entryRef == pitEntry.get()) {
        continue; // skip the current interest itself
    }

    // Get the set of IDs for the existing pending interest
    std::set<int> existingIds;
    for (size_t i = 1; i < existingName.size(); ++i) {
      try {
        existingIds.insert(std::stoi(existingName[i].toUri()));
      } catch (...) { }
    }

    // Check subset/superset relation
    bool existingIsSuperset = std::includes(existingIds.begin(), existingIds.end(),
                                            requestedIds.begin(), requestedIds.end());
    bool existingIsSubset   = std::includes(requestedIds.begin(), requestedIds.end(),
                                            existingIds.begin(), existingIds.end());
    if (existingIsSuperset) {
      // An already-pending Interest covers all of this new Interest's requirements (and possibly more).
      // Piggyback: no need to forward new Interest; attach it to the existing one.
      std::cout << "  [Piggyback] Interest " << interestName.toUri() 
                << " piggybacks on superset Interest " << existingName.toUri() << std::endl;
      // Add this PIT entry to superset interest's dependents list
      AggregatePitInfo* supersetInfo = entryRef.getStrategyInfo<AggregatePitInfo>();
      if (supersetInfo) {
        supersetInfo->dependentInterests.push_back(pitEntry);
      }
      // Do not forward this Interest (it will be satisfied when the superset returns)
      return;
    }
    else if (existingIsSubset) {
      // A pending Interest is a subset of the new Interest (i.e., part of new Interest's data is already being fetched).
      std::cout << "  [Subset] Interest " << existingName.toUri() 
                << " is a subset of new Interest " << interestName.toUri() << std::endl;
      // We can piggyback on the existing subset for overlapping IDs, and only fetch the missing IDs.
      // Overlapping IDs = existingIds (the subset)
      for (int overlapId : existingIds) {
        if (pitInfo->pendingIds.find(overlapId) != pitInfo->pendingIds.end()) {
          // Remove overlapping IDs from new Interest's pending set (they will be provided by subset interest)
          pitInfo->pendingIds.erase(overlapId);
        }
      }
      // Link new interest to wait for the subset's Data
      Name subsetDataName = entryRef.getName(); // data will use the same name as interest
      m_waitingInterests[subsetDataName].push_back(pitEntry);
      // Now, proceed to forward requests for the remaining (non-overlap) IDs only
      // (We break out of loop to perform forwarding of the rest)
      break;
    }
  }

  // If some IDs are still pending (either initial missing or those not covered by subset piggyback), we will forward Interests for them.
  if (!pitInfo->pendingIds.empty()) {
    // ** 3. Perform Interest Splitting based on routing (FIB) to minimize redundant requests **

    // Group pending IDs by next-hop face (using FIB longest prefix match for each ID)
    std::map<Face*, std::vector<int>> faceToIdsMap;
    Fib& fib = m_forwarder.getFib();
    for (int id : pitInfo->pendingIds) {
      // Construct a name for the individual ID (assuming producers are reachable via prefix "/aggregate/<id>")
      Name idName;
      idName.append("aggregate").appendNumber(id);
      // BUG FIX
      const nfd::fib::Entry& fibEntry = fib.findLongestPrefixMatch(idName); 
      if (fibEntry.getPrefix().empty()) {
        continue; // No route for this ID
      }
      // Choose the best next hop from FIB entry (lowest cost or highest rank)
      const fib::NextHopList& nexthops = fibEntry.getNextHops();
      if (nexthops.empty()) continue;
      const fib::NextHop& nh = *nexthops.begin(); // pick the first next hop (by metric order)
      Face& outFace = nh.getFace();
      faceToIdsMap[&outFace].push_back(id);
    }

    // For each outgoing face, create a sub-Interest covering the IDs assigned to that face
    for (auto& faceGroup : faceToIdsMap) {
      Face* outFace = faceGroup.first;
      std::vector<int>& idList = faceGroup.second;
      if (idList.empty()) continue;
      // Sort IDs to create a canonical order in the interest name
      std::sort(idList.begin(), idList.end());
      // Build the Name for the sub-interest: "/aggregate/id1/id2/..."
      Name subInterestName;
      subInterestName.append("aggregate");
      for (int id : idList) {
        subInterestName.appendNumber(id);
      }
      Interest subInterest(subInterestName);
      subInterest.setCanBePrefix(false);
      subInterest.setInterestLifetime(interest.getInterestLifetime()); // inherit lifetime from original
      // Forward the sub-interest out on the chosen face
      this->sendInterest(subInterest, *outFace, pitEntry);
      // Record the mapping of this sub-interest to the parent (for data re-assembly)
      m_parentMap[subInterestName] = pitEntry;
      std::cout << "  >> Forwarded sub-Interest " << subInterestName.toUri() 
                << " to face " << outFace->getId() << std::endl;
    }
    // All necessary sub-interests have been forwarded.
    // We will wait for their Data responses to aggregate.
  }
  else {
    // If no pending IDs to forward (somehow fully piggybacked on existing interests), we just wait.
    std::cout << "  (No new sub-interests forwarded for " << interestName.toUri() << ")" << std::endl;
  }

  // Note: We do NOT call sendInterest on the original Interest name itself, since it has been split.
  // We keep the PIT entry alive until all needed data arrives (handled in afterReceiveData).
  this->setExpiryTimer(pitEntry, interest.getInterestLifetime());
}

// ** Handling incoming Data packets (from upstream) **
void 
AggregateStrategy::afterReceiveData(const Data& data, const FaceEndpoint& ingress,
                                   const shared_ptr<pit::Entry>& pitEntry) 
{
  Name dataName = data.getName();
  std::cout << "<< Data received: " << dataName.toUri() 
            << " from face " << ingress.face.getId() << std::endl;

  // Check if this Data corresponds to a sub-Interest that was sent out (i.e., part of an aggregated request)
  auto parentIt = m_parentMap.find(dataName);
  if (parentIt != m_parentMap.end()) {
    // This data is a response to a sub-interest that our strategy created.
    // Retrieve the parent PIT entry that initiated this sub-interest
    std::shared_ptr<pit::Entry> parentPit = parentIt->second.lock();
    if (parentPit) {
      AggregatePitInfo* parentInfo = parentPit->getStrategyInfo<AggregatePitInfo>();
      if (parentInfo) {
        // Parse the content of the Data to extract the numeric value (partial sum or single value)
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

        // Determine which IDs this data covers (from its name components)
        std::set<int> dataIds;
        for (size_t i = 1; i < dataName.size(); ++i) {
          try {
            dataIds.insert(std::stoi(dataName[i].toUri()));
          } catch (...) { }
        }

        // Update parent's partial sum and mark these IDs as fulfilled
        parentInfo->partialSum += value;
        for (int fulfilledId : dataIds) {
          parentInfo->pendingIds.erase(fulfilledId);
          // If this was a single-ID data, cache it for future Interests
          if (dataIds.size() == 1) {
            m_cachedValues[fulfilledId] = value;  // store in local cache
          }
        }
        std::cout << "    [Aggregation] Data " << dataName.toUri() << " contributes value " 
                  << value << " to parent Interest " << parentPit->getName().toUri() << std::endl;
        std::cout << "    Remaining IDs for parent: { ";
        for (int pid : parentInfo->pendingIds) std::cout << pid << " ";
        std::cout << "}" << std::endl;

        // If all components for the parent interest have arrived (pending set empty), produce the aggregated Data
        if (parentInfo->pendingIds.empty()) {
          uint64_t totalSum = parentInfo->partialSum;
          Name parentName = parentPit->getName();
          auto aggData = std::make_shared<ndn::Data>(parentName);
          uint64_t sumNbo = htobe64(totalSum);
          std::shared_ptr<ndn::Buffer> buffer = std::make_shared<ndn::Buffer>(reinterpret_cast<const uint8_t*>(&sumNbo), sizeof(sumNbo));
          aggData->setContent(buffer);
          aggData->setFreshnessPeriod(::ndn::time::milliseconds(1000));
          // WITH THIS CORRECT VERSION:
            for (const auto& inRecord : pitEntry->getInRecords()) {
                Face& outFace = inRecord.getFace();
                this->sendData(*aggData, outFace, parentPit);
            }
          std::cout << "<< Generated aggregate Data " << parentName.toUri() 
                    << " with sum = " << totalSum << std::endl;

          // Cache the aggregated result in local store as well
          // (so future identical Interests can be satisfied quickly)
          // We store aggregated results by each component ID as well, for reuse in subsets.
          for (int id : parentInfo->neededIds) {
            // If the parent covered multiple IDs, we only cache the final combined sum under the full name,
            // but caching each individual id's value is already handled when those atomic pieces arrived.
          }

          // If any other Interests were piggybacking on this parent Interest, satisfy them as well
          for (auto& weakChildPit : parentInfo->dependentInterests) {
            auto childPit = weakChildPit.lock();
            if (!childPit) continue;
            // Compute the subset sum for the child (it might be different from totalSum if parent had extra IDs)
            AggregatePitInfo* childInfo = childPit->getStrategyInfo<AggregatePitInfo>();
            if (childInfo) {
              // Sum up values for child's needed IDs from our cached values (all should be available now)
              uint64_t childSum = 0;
              for (int cid : childInfo->neededIds) {
                // If child's needed ID is in parent's needed, we should have its value (either cached atomic or part of totalSum).
                // If parent's interest had extra IDs not needed by child, those are simply ignored in child's sum.
                if (m_cachedValues.find(cid) != m_cachedValues.end()) {
                  childSum += m_cachedValues[cid];
                }
              }
              // Send Data to satisfy the child interest
              Name childName = childPit->getName();
              auto childData = std::make_shared<ndn::Data>(childName);
              uint64_t childSumNbo = htobe64(childSum);
              // With one of these alternatives:
              // Option 1:
              std::shared_ptr<ndn::Buffer> buffer = std::make_shared<ndn::Buffer>(reinterpret_cast<const uint8_t*>(&childSumNbo), sizeof(childSumNbo));
              childData->setContent(buffer);
              childData->setFreshnessPeriod(::ndn::time::milliseconds(1000));
              // WITH THIS CORRECT VERSION:
                for (const auto& inRecord : pitEntry->getInRecords()) {
                    Face& outFace = inRecord.getFace();
                    this->sendData(*childData, outFace, childPit);
                }
              std::cout << "<< Satisfied piggybacked Interest " << childName.toUri() 
                        << " with sum = " << childSum << std::endl;
            }
          }
        }
      }
    }
    // Remove the mapping as this sub-interest’s data has been processed
    m_parentMap.erase(dataName);
  }

  // Next, check if this Data corresponds to an interest that other PIT entries were waiting on (piggybacked subset case)
  auto waitIt = m_waitingInterests.find(dataName);
  if (waitIt != m_waitingInterests.end()) {
    // One or more interests were waiting for this data (they piggybacked on some other interest that fetched this data).
    for (auto& weakPit : waitIt->second) {
      auto waitingPit = weakPit.lock();
      if (!waitingPit) continue;
      AggregatePitInfo* waitingInfo = waitingPit->getStrategyInfo<AggregatePitInfo>();
      if (!waitingInfo) continue;
      // The Data received covers all IDs in 'dataName'. If those IDs are part of the waiting interest, mark them satisfied.
      std::set<int> dataIds;
      for (size_t i = 1; i < dataName.size(); ++i) {
        try {
          dataIds.insert(std::stoi(dataName[i].toUri()));
        } catch (...) { }
      }
      uint64_t value = 0;
      if (data.getContent().value_size() >= sizeof(uint64_t)) {
        uint64_t netBytes;
        std::memcpy(&netBytes, data.getContent().value(), sizeof(uint64_t));
        value = be64toh(netBytes);
      } else {
        std::string text(reinterpret_cast<const char*>(data.getContent().value()),
                         data.getContent().value_size());
        try { value = std::stoull(text); } catch (...) { value = 0; }
      }
      // If the data was an aggregate of multiple IDs, we cannot extract individual values, 
      // so we'll treat it as one combined piece.
      // Add to waiting interest's partial sum and remove those IDs from pending.
      waitingInfo->partialSum += value;
      for (int gotId : dataIds) {
        waitingInfo->pendingIds.erase(gotId);
      }
      std::cout << "    [Piggyback] Data " << dataName.toUri() << " received for waiting Interest " 
                << waitingPit->getName().toUri() << std::endl;
      // If that waiting interest now has no pending IDs left, we can produce its final Data.
      if (waitingInfo->pendingIds.empty()) {
        uint64_t totalSum = waitingInfo->partialSum;
        Name waitName = waitingPit->getName();
        auto finalData = std::make_shared<ndn::Data>(waitName);
        uint64_t sumNbo = htobe64(totalSum);
        // With one of these alternatives:
        // Option 1:
        std::shared_ptr<ndn::Buffer> buffer = std::make_shared<ndn::Buffer>(reinterpret_cast<const uint8_t*>(&sumNbo), sizeof(sumNbo));
        finalData->setContent(buffer);
        finalData->setFreshnessPeriod(::ndn::time::milliseconds(1000));
        
        // WITH THIS CORRECT VERSION:
        for (const auto& inRecord : pitEntry->getInRecords()) {
            Face& outFace = inRecord.getFace();
            this->sendData(*finalData, outFace, waitingPit);
        }

        std::cout << "<< Delivered aggregated Data " << waitName.toUri() 
                  << " with sum = " << totalSum << " (via piggyback)" << std::endl;
      }
    }
    // Remove this entry from waiting list after processing
    m_waitingInterests.erase(waitIt);
  }

  // If this Data was for an original aggregate Interest (no parent, not a sub-interest):
  // In such case, the PIT entry would normally be satisfied automatically. Just ensure caching:
  if (m_parentMap.find(dataName) == m_parentMap.end()) {
    // If dataName is atomic, cache its value
    if (dataName.size() == 2) { // form "/aggregate/<id>"
      try {
        int id = std::stoi(dataName.get(1).toUri());
        uint64_t val = 0;
        if (data.getContent().value_size() >= sizeof(uint64_t)) {
          uint64_t netBytes;
          std::memcpy(&netBytes, data.getContent().value(), sizeof(uint64_t));
          val = be64toh(netBytes);
        }
        else {
          std::string text(reinterpret_cast<const char*>(data.getContent().value()),
                           data.getContent().value_size());
          try { val = std::stoull(text); } catch (...) { val = 0; }
        }
        m_cachedValues[id] = val;
        std::cout << "  [CacheStore] Cached value for ID " << id << " = " << val << std::endl;
      } catch (...) { /* not an integer component */ }
    }
    // For aggregated data covering multiple IDs, we could store the sum by full name if needed.
  }

  // ** Forward the Data down to any PIT downstreams as usual (if not already handled) **
  // If this Data satisfies a PIT entry that has not been manually satisfied above, 
  // we rely on default behavior to forward it downstream.
  // WITH THIS CORRECT VERSION:
    for (const auto& inRecord : pitEntry->getInRecords()) {
        Face& outFace = inRecord.getFace();
        this->sendData(data, outFace, pitEntry);
    }
}

} // namespace fw
} // namespace nfd
