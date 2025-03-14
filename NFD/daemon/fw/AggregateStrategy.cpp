// Add explicit include for GlobalValue at the top with other includes:
#include "ns3/core-module.h"
#include "ns3/global-value.h"  // Add this explicit include

// Add this include near the top, with other includes
#include "AggregateStrategy.hpp"
#include "ns3/ndnSIM/NFD/daemon/table/fib.hpp"
#include <ndn-cxx/data.hpp>

// Add these NS3-related includes
#include "ns3/node-container.h"
#include "ns3/node-list.h"
#include "ns3/simulator.h"
#include "ns3/uinteger.h" // Add this for UintegerValue
#include "ns3/config.h"   // Add this for Config::GetGlobal

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
  , m_nodeId(ns3::NodeContainer::GetGlobal().Get(ns3::Simulator::GetContext())->GetId() + 1)
  , m_nodeRole(NodeRole::UNKNOWN)
  , m_logicalId(0)
{
  // Set the instance name explicitly
  this->setInstanceName(name);

  // Determine node role and logical ID for logging
  determineNodeRole();
  std::cout << getNodeRoleString() << " initialized AggregateStrategy" << std::endl;

  // Register for PIT expiration
  registerPitExpirationCallback();

  std::cout << "AggregateStrategy initialized for Forwarder." << std::endl;
  std::cout << "Strategy will use virtual method overrides." << std::endl << std::flush;  
}

// Add these new methods:
void 
AggregateStrategy::determineNodeRole() 
{
  // Get total nodes in simulation
  uint32_t totalNodes = ns3::NodeContainer::GetGlobal().GetN();
  
  // Get nodeCount from GlobalValue system
  uint32_t nodeCount = 0;
  ns3::UintegerValue val;
  bool exists = false;
  
  // Use GetValueByNameFailSafe instead - this returns a bool
  exists = ns3::GlobalValue::GetValueByNameFailSafe("NodeCount", val);
  
  if (exists) {
    nodeCount = val.Get();
    std::cout << "Strategy found nodeCount=" << nodeCount << " from GlobalValue" << std::endl;
  } else {
    // Fallback if not found
    nodeCount = std::max(2u, totalNodes / 3);
    std::cout << "Strategy using fallback nodeCount=" << nodeCount << std::endl;
  }
  
  // Calculate topology elements - match exactly with aggregate-sum-simulation.cpp
  uint32_t numRackAggregators = nodeCount;  // One rack aggregator per producer
  uint32_t numCoreAggregators = (nodeCount > 1) ? std::max(1u, nodeCount / 4) : 0;
  
  // Get 0-based node index (m_nodeId is already correctly set in constructor)
  uint32_t nodeIndex = m_nodeId - 1;
  
  // Now determine role based on index ranges from the topology creation
  if (nodeIndex < nodeCount) {
    // First nodeCount nodes are producers (0 to nodeCount-1)
    m_nodeRole = NodeRole::PRODUCER;
    m_logicalId = nodeIndex + 1;  // 1-indexed for display
  }
  else if (nodeIndex < nodeCount + numRackAggregators) {
    // Next numRackAggregators nodes are rack aggregators
    m_nodeRole = NodeRole::RACK_AGG;
    m_logicalId = nodeIndex - nodeCount + 1;  // 1-indexed for display
  }
  else {
    // Remaining nodes are core aggregators
    m_nodeRole = NodeRole::CORE_AGG;
    m_logicalId = nodeIndex - (nodeCount + numRackAggregators) + 1;  // 1-indexed for display
  }
}

std::string 
AggregateStrategy::getNodeRoleString() const 
{
  switch (m_nodeRole) {
    case NodeRole::PRODUCER:
      return "P" + std::to_string(m_logicalId);
    case NodeRole::RACK_AGG:
      return "R" + std::to_string(m_logicalId);
    case NodeRole::CORE_AGG:
      return "C" + std::to_string(m_logicalId);
    default:
      return "NODE " + std::to_string(m_nodeId);
  }
}

// Helper: parse interest name of form /aggregate/<id1>/<id2>/... into a set of integers
std::set<int> 
AggregateStrategy::parseRequestedIds(const Interest& interest) const {
  std::set<int> idSet;
  const Name& name = interest.getName();
  
  // Skip the first component ("aggregate") and skip sequence number if present
  for (size_t i = 1; i < name.size(); ++i) {
    // Skip sequence number components (they have a specific format)
    if (name[i].isSequenceNumber() || name[i].toUri().find("seq=") != std::string::npos) {
      continue;
    }
    
    try {
      int id = 0;
      if (name[i].isNumber()) {
        id = name[i].toNumber();
        // Only include IDs that make sense (skip 0)
        if (id > 0) {
          idSet.insert(id);
        }
      }
    } catch (const std::exception& e) {
      // Skip invalid components
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
  std::cout << '\n' << getNodeRoleString()
          << " - STRATEGY received Interest: " << interest.getName() 
          << " via " << ingress.face.getId() 
          << " at " << std::fixed << std::setprecision(2) << ns3::Simulator::Now().GetSeconds() 
          << "s" << std::endl << std::flush;

  // Dump the current PIT entries for debugging:
  std::cout << "Current PIT entries before forwarding Interest:" << std::endl;
  Pit &pit = m_forwarder.getPit();
  for (auto it = pit.begin(); it != pit.end(); ++it) {
      // Do not try to copy *it; instead, bind it to a const reference.
      const nfd::pit::Entry &entry = *it;
      std::cout << "  PIT entry: " << entry.getName() 
                << " (InFaces=" << entry.getInRecords().size() 
                << ", OutFaces=" << entry.getOutRecords().size() << ")" << std::endl;
  }

  // Get a reference to the FIB table first
  Fib& fib = m_forwarder.getFib();

  // Debug: Print FIB size and pending IDs
  std::cout << "DEBUG: FIB table has " << std::distance(fib.begin(), fib.end()) << " entries" << std::endl << std::flush;

  // Add this code to dump the complete FIB table content
  std::cout << "DEBUG: Current FIB entries:" << std::endl;
  for (const auto& fibEntry : fib) {
    std::cout << "  - Prefix: " << fibEntry.getPrefix() << " (Nexthops: " << fibEntry.getNextHops().size() << ")" << std::endl;
    for (const auto& nh : fibEntry.getNextHops()) {
      std::cout << "    * Face: " << nh.getFace().getId() << " Cost: " << nh.getCost() << std::endl;
    }
  }

  // COMBINED INTEREST AGGREGATION CHECK
  // Check #1: Interest has already been forwarded (has OutFaces)
  if (pitEntry->hasOutRecords()) {
    bool isSameFaceDuplicate = false;
    bool isDifferentFaceDuplicate = false;
    
    for (const auto& inRecord : pitEntry->getInRecords()) {
      if (inRecord.getFace().getId() == ingress.face.getId()) {
        // This exact same face already has an in-record
        isSameFaceDuplicate = true;
      } else {
        // A different face has an in-record
        isDifferentFaceDuplicate = true;
      }
    }
    
    if (isSameFaceDuplicate) {
      std::cout << "  [Interest Aggregation] Duplicate interest from same face detected" << std::endl;
      std::cout << "  [Interest Aggregation] Interest " << interest.getName() 
                << " already forwarded - suppressing redundant forwarding" << std::endl;
      return; // Exit without forwarding again - this is a duplicate from same face
    }
    
    if (isDifferentFaceDuplicate) {
      std::cout << "  [Interest Aggregation] Duplicate interest from different face detected" << std::endl;
      std::cout << "  [Interest Aggregation] Interest " << interest.getName() 
                << " aggregated (added face " << ingress.face.getId() 
                << " to existing PIT entry)" << std::endl;
      return; // Exit without forwarding again
    }
  }
  
  // Check #2: Another PIT entry exists with the same name and has been forwarded
  for (auto it = pit.begin(); it != pit.end(); ++it) {
    const nfd::pit::Entry &entry = *it;
    
    // Skip if it's the same entry we're currently processing
    if (&entry == pitEntry.get()) {
      continue;
    }
    
    // If same name but different entry, we have a duplicate that should be aggregated
    if (entry.getName() == interest.getName() && entry.hasOutRecords()) {
      std::cout << "  [Interest Aggregation] Duplicate interest " << interest.getName() 
                << " detected across different PIT entries" << std::endl;
      std::cout << "  [Interest Aggregation] Original PIT entry with "
                << entry.getInRecords().size() << " in-faces and "
                << entry.getOutRecords().size() << " out-faces" << std::endl;
      
      // The system should merge these eventually, but in some NDN implementations
      // we need to manually ensure both entries are linked
      
      // Don't forward this interest again - data will be returned to all faces
      return;
    }
  }
          
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
      std::cout << "[Strategy] Forwarding regular Interest " 
              << interestName << " to face " << outFace.getId() << std::endl;
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
  std::cout << "}" << std::endl << std::flush;

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
                << cachedValue << " (from CS)" << std::endl << std::flush;
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
              << " from cache with sum = " << totalSum << std::endl << std::flush;
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
      // With this code that handles both formats:
      if (existingName[i].isNumber()) {
          existingIds.insert(existingName[i].toNumber());
        } else {
          try {
            existingIds.insert(std::stoi(existingName[i].toUri()));
          } catch (...) { }
        }
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
                << " piggybacks on superset Interest " << existingName.toUri() << std::endl << std::flush;
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
                << " is a subset of new Interest " << interestName.toUri() << std::endl << std::flush;
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
    }
  }

  // If some IDs are still pending (either initial missing or those not covered by subset piggyback), we will forward Interests for them.
  if (!pitInfo->pendingIds.empty()) {
    std::cout << "If some IDs are still pending" << std::endl << std::flush;
    // ** 3. Perform Interest Splitting based on routing (FIB) to minimize redundant requests **

    // Group pending IDs by next-hop face (using FIB longest prefix match for each ID)
    std::map<Face*, std::vector<int>> faceToIdsMap;
    Fib& fib = m_forwarder.getFib();
    
    // Debug: Print FIB size and pending IDs
    std::cout << "DEBUG: FIB table has " << std::distance(fib.begin(), fib.end()) << " entries" << std::endl << std::flush;
    std::cout << "DEBUG: Processing " << pitInfo->pendingIds.size() << " pending IDs: [ ";
    for (int id : pitInfo->pendingIds) {
      std::cout << id << " ";
    }
    std::cout << "]" << std::endl << std::flush;
    
    // Map IDs to faces first
    for (int id : pitInfo->pendingIds) {
      // Construct a name for the individual ID
      Name idName("/aggregate");
      idName.appendNumber(id);
      std::cout << "DEBUG: Looking up FIB entry for ID " << id << ", Name: " << idName << std::endl << std::flush;
      
      const nfd::fib::Entry& fibEntry = fib.findLongestPrefixMatch(idName);
      if (fibEntry.getPrefix().empty() || fibEntry.getNextHops().empty()) {
        std::cout << "DEBUG: No route found for ID " << id << ", skipping..." << std::endl << std::flush;
        continue;
      }
      
      const fib::NextHop& nh = *fibEntry.getNextHops().begin();
      Face& outFace = nh.getFace();
      std::cout << "DEBUG: Selected Face " << outFace.getId() << " for ID " << id << std::endl << std::flush;
      
      faceToIdsMap[&outFace].push_back(id);
    }

    // NEW OPTIMIZATION: If all IDs map to exactly one face, forward only the pending IDs to that face
    if (faceToIdsMap.size() == 1 && faceToIdsMap.begin()->second.size() == pitInfo->pendingIds.size()) {
      Face* outFace = faceToIdsMap.begin()->first;
      std::cout << "OPTIMIZATION: All " << pitInfo->pendingIds.size() 
                << " IDs route to the same face (ID: " << outFace->getId() 
                << ")." << std::endl;
      
      // Create a new interest containing only the pending IDs instead of forwarding the original interest
      std::vector<int> pendingIdsList(pitInfo->pendingIds.begin(), pitInfo->pendingIds.end());
      std::sort(pendingIdsList.begin(), pendingIdsList.end());
      
      // Build name with only pending IDs: /aggregate/id1/id2/...
      Name optimizedName;
      optimizedName.append("aggregate");
      for (int id : pendingIdsList) {
        optimizedName.appendNumber(id);
      }
      
      // If the sequence number was in the original interest, preserve it
      for (size_t i = 0; i < interest.getName().size(); i++) {
        if (interest.getName()[i].toUri().find("seq=") != std::string::npos) {
          optimizedName.append(interest.getName()[i]);
          break;
        }
      }

      std::cout << "  >> Creating optimized interest with only pending IDs: " 
                << optimizedName << std::endl;
                
      // Create and forward the optimized interest
      auto optimizedInterest = std::make_shared<Interest>(optimizedName);
      optimizedInterest->setCanBePrefix(false);
      optimizedInterest->setInterestLifetime(interest.getInterestLifetime());
      
      // Create a new PIT entry for the optimized interest instead of using the original one
      auto newPitEntry = m_forwarder.getPit().insert(*optimizedInterest).first;
      
      // Store relationship between new and original PIT entries
      AggregateSubInfo* subInfo = newPitEntry->insertStrategyInfo<AggregateSubInfo>().first;
      if (subInfo) {
        subInfo->parentEntry = pitEntry;
      }
      
      // Add to parent map to track relationship
      m_parentMap[optimizedName] = pitEntry;
      
      // Send using the new PIT entry
      this->sendInterest(*optimizedInterest, *outFace, newPitEntry);
      
      return;
    }

    // Debug output for face-to-IDs mapping
    std::cout << "DEBUG: Face-to-IDs mapping results:" << std::endl << std::flush;
    for (const auto& pair : faceToIdsMap) {
      std::cout << "  - Face ID " << pair.first->getId() << " will handle IDs: [ ";
      for (int id : pair.second) {
        std::cout << id << " ";
      }
      std::cout << "]" << std::endl << std::flush;
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

      // add the sub-interest into the parent map
      m_parentMap[subInterestName] = pitEntry;

      // *** Add these lines: ***
      const nfd::fib::Entry& fibEntry = fib.findLongestPrefixMatch(subInterestName);
      std::cout << ">> Forwarding sub-Interest " << subInterestName.toUri()
                << " via face " << outFace->getId()
                << " (FIB match: " << fibEntry.getPrefix().toUri() << ")" << std::endl;
      // *** End addition ***

      // create Interest with shared_ptr to avoid bad_weak_ptr exception
      std::shared_ptr<ndn::Interest> subInterest = std::make_shared<Interest>(subInterestName);
      subInterest->setCanBePrefix(false);
      subInterest->setInterestLifetime(interest.getInterestLifetime());
      // Insert a copy of the Interest to avoid bad_weak_ptr
      auto temporaryEntry = m_forwarder.getPit().insert(*subInterest).first;

      this->sendInterest(*subInterest, *outFace, temporaryEntry);

      // After forwarding, find the newly created PIT entry and attach our metadata
      shared_ptr<pit::Entry> subPitEntry = m_forwarder.getPit().find(*subInterest);

      if (subPitEntry) {
        // Store parent-child relationship using explicit typing
        AggregateSubInfo* subInfo = subPitEntry->insertStrategyInfo<AggregateSubInfo>().first;
        if (subInfo) {
          subInfo->parentEntry = pitEntry;
          
          std::cout << "  >> Forwarded sub-Interest " << subInterestName.toUri() 
                    << " to face " << outFace->getId() 
                    << " (new PIT entry created)" << std::endl << std::flush;
        }
      }
    }
    // All necessary sub-interests have been forwarded.
    // We will wait for their Data responses to aggregate.
  }
  else {
    // If no pending IDs to forward (somehow fully piggybacked on existing interests), we just wait.
    std::cout << "  (No new sub-interests forwarded for " << interestName.toUri() << ")" << std::endl << std::flush;
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
  // Strategy::afterReceiveData(data, ingress, pitEntry); // call base to forward data to pending faces
  Name dataName = data.getName();
  std::cout << "<< Data received: " << dataName.toUri() 
            << " from face " << ingress.face.getId() << std::endl << std::flush;

  // Dump the current PIT entries for debugging:
  std::cout << "Current PIT entries before processing Data:" << std::endl;
  Pit &pit = m_forwarder.getPit();
  for (auto it = pit.begin(); it != pit.end(); ++it) {
      const nfd::pit::Entry &entry = *it;
      std::cout << "  PIT entry: " << entry.getName() 
                << " (InFaces=" << entry.getInRecords().size() 
                << ", OutFaces=" << entry.getOutRecords().size() << ")" << std::endl;
  }

  // Check if this Data corresponds to a sub-Interest that was sent out (i.e., part of an aggregated request)
  auto parentIt = m_parentMap.find(dataName);
  if (parentIt != m_parentMap.end()) {
    std::cout << "  [SubInterest] Found matching parent for Data " << dataName.toUri() << std::endl << std::flush;
    // This data is a response to a sub-interest that our strategy created.
    // Retrieve the parent PIT entry that initiated this sub-interest
    std::shared_ptr<pit::Entry> parentPit = parentIt->second.lock();
    if (parentPit) {
      AggregatePitInfo* parentInfo = parentPit->getStrategyInfo<AggregatePitInfo>();
      if (parentInfo) {
        std::cout << "  [SubInterest] Processing Data for parent Interest " << parentPit->getName().toUri() << std::endl << std::flush;
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
            std::cout << "  [Cache] Stored value " << value << " for single ID " << fulfilledId << std::endl << std::flush;
          }
        }
        std::cout << "    [Aggregation] Data " << dataName.toUri() << " contributes value " 
                  << value << " to parent Interest " << parentPit->getName().toUri() << std::endl;
        std::cout << "    Remaining IDs for parent: { ";
        for (int pid : parentInfo->pendingIds) std::cout << pid << " ";
        std::cout << "}" << std::endl << std::flush;

        // If all components for the parent interest have arrived (pending set empty), produce the aggregated Data
        if (parentInfo->pendingIds.empty()) {
          std::cout << "  [SubInterest] All components received, creating final aggregated Data" << std::endl << std::flush;
          uint64_t totalSum = parentInfo->partialSum;
          Name parentName = parentPit->getName();
          auto aggData = std::make_shared<ndn::Data>(parentName);
          uint64_t sumNbo = htobe64(totalSum);
          std::shared_ptr<ndn::Buffer> buffer = std::make_shared<ndn::Buffer>(reinterpret_cast<const uint8_t*>(&sumNbo), sizeof(sumNbo));
          aggData->setContent(buffer);
          aggData->setFreshnessPeriod(::ndn::time::milliseconds(1000));
          // WITH THIS CORRECT VERSION (BUG FIX WITH parentPit->):
            for (const auto& inRecord : parentPit->getInRecords()) {
                Face& outFace = inRecord.getFace();
                this->sendData(*aggData, outFace, parentPit);
            }
          std::cout << "<< Generated aggregate Data " << parentName.toUri() 
                    << " with sum = " << totalSum << std::endl << std::flush;

          // Cache the aggregated result in local store as well
          // (so future identical Interests can be satisfied quickly)
          // We store aggregated results by each component ID as well, for reuse in subsets.
          for (int id : parentInfo->neededIds) {
            // If the parent covered multiple IDs, we only cache the final combined sum under the full name,
            // but caching each individual id's value is already handled when those atomic pieces arrived.
          }

          // If any other Interests were piggybacking on this parent Interest, satisfy them as well
          if (!parentInfo->dependentInterests.empty()) {
            std::cout << "  [SubInterest] Satisfying " << parentInfo->dependentInterests.size() 
                      << " piggybacked child interests" << std::endl << std::flush;
          }
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

              // NEW (corrected) -- BUG FIX: use childPit instead of pitEntry->getInRecords()
              for (const auto& inRecord : childPit->getInRecords()) {
                  Face& outFace = inRecord.getFace();
                  this->sendData(*childData, outFace, childPit);
              }
              std::cout << "<< Satisfied piggybacked Interest " << childName.toUri() 
                        << " with sum = " << childSum << std::endl;
            }
          }
        }
      }
      else {
        std::cout << "  [SubInterest] No strategy info found for parent PIT entry" << std::endl << std::flush;
      }
    }
    else {
      std::cout << "  [SubInterest] Parent PIT entry already expired" << std::endl << std::flush;
    }
    // Remove the mapping as this sub-interest's data has been processed
    m_parentMap.erase(dataName);
  }

  // Next, check if this Data corresponds to an interest that other PIT entries were waiting on (piggybacked subset case)
  auto waitIt = m_waitingInterests.find(dataName);
  if (waitIt != m_waitingInterests.end()) {
    std::cout << "  [WaitingInterest] Found " << waitIt->second.size() 
              << " interests waiting for Data " << dataName.toUri() << std::endl << std::flush;
    // One or more interests were waiting for this data (they piggybacked on some other interest that fetched this data).
    for (auto& weakPit : waitIt->second) {
      auto waitingPit = weakPit.lock();
      if (!waitingPit) {
        std::cout << "  [WaitingInterest] Skipping expired waiting interest" << std::endl << std::flush;
        continue;
      }
      AggregatePitInfo* waitingInfo = waitingPit->getStrategyInfo<AggregatePitInfo>();
      if (!waitingInfo) {
        std::cout << "  [WaitingInterest] Skipping waiting interest without strategy info" << std::endl << std::flush;
        continue;
      }
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
        std::cout << "  [WaitingInterest] All components received for waiting interest, creating final Data" << std::endl << std::flush;
        uint64_t totalSum = waitingInfo->partialSum;
        Name waitName = waitingPit->getName();
        auto finalData = std::make_shared<ndn::Data>(waitName);
        uint64_t sumNbo = htobe64(totalSum);
        // With one of these alternatives:
        // Option 1:
        std::shared_ptr<ndn::Buffer> buffer = std::make_shared<ndn::Buffer>(reinterpret_cast<const uint8_t*>(&sumNbo), sizeof(sumNbo));
        finalData->setContent(buffer);
        finalData->setFreshnessPeriod(::ndn::time::milliseconds(1000));
        
        // WITH THIS CORRECT VERSION (BUG FIX WITH waitingPit->):
        for (const auto& inRecord : waitingPit->getInRecords()) {
            Face& outFace = inRecord.getFace();
            this->sendData(*finalData, outFace, waitingPit);
        }

        std::cout << "<< Delivered aggregated Data " << waitName.toUri() 
                  << " with sum = " << totalSum << " (via piggyback)" << std::endl << std::flush;
      }
      else {
        std::cout << "  [WaitingInterest] Interest still waiting for " << waitingInfo->pendingIds.size() 
                  << " more IDs" << std::endl << std::flush;
      }
    }
    // Remove this entry from waiting list after processing
    m_waitingInterests.erase(waitIt);
  }

  // If this Data was for an original aggregate Interest (no parent, not a sub-interest):
  if (m_parentMap.find(dataName) == m_parentMap.end()) {
    std::cout << "  [DirectData] Processing regular Data packet (not sub-interest)" << std::endl << std::flush;
    // If dataName is atomic, cache its value
    if (dataName.size() == 2) { // form "/aggregate/<id>"
      std::cout << "  [DirectData] Processing atomic data for single ID" << std::endl << std::flush;
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
        std::cout << "  [CacheStore] Cached value for ID " << id << " = " << val << std::endl << std::flush;
      } catch (...) { 
        std::cout << "  [DirectData] Failed to parse ID as integer" << std::endl << std::flush;
      }
    }
  }

  // ** Forward the Data down to any PIT downstreams as usual (if not already handled) **
  
  // WITH THIS CORRECT VERSION:
  int recordCount = 0;
  for (const auto& inRecord : pitEntry->getInRecords()) {
      Face& outFace = inRecord.getFace();
      std::cout << "[Forward] Sending Data " << data.getName() 
              << " to face " << outFace.getId() << std::endl;
      this->sendData(data, outFace, pitEntry);
      recordCount++;
  }
  std::cout << "  [Forward] Forwarding Data to " << recordCount << " downstream faces" << std::endl << std::flush;
}

void 
AggregateStrategy::registerPitExpirationCallback()
{
  // Register a callback for PIT entry expiration using NFD's signal mechanism
  m_forwarder.beforeExpirePendingInterest.connect(
    [this] (const pit::Entry& pitEntry) {  // Changed from shared_ptr to const&
      std::cout << "!! PIT EXPIRED: " << pitEntry.getName().toUri()
                << " at " << std::fixed << std::setprecision(2) << ns3::Simulator::Now().GetSeconds() 
                << "s" << std::endl << std::flush;
      
      // Log details about the expired entry (use . instead of ->)
      AggregatePitInfo* pitInfo = pitEntry.getStrategyInfo<AggregatePitInfo>();
      if (pitInfo) {
        std::cout << "  [Expired] " << pitInfo->pendingIds.size() << " pending IDs: { ";
        for (int id : pitInfo->pendingIds) {
          std::cout << id << " ";
        }
        std::cout << "}" << std::endl << std::flush;
      }
    });
  std::cout << "PIT expiration handler registered!" << std::endl << std::flush;
}

void
AggregateStrategy::beforeExpirePendingInterest(const shared_ptr<pit::Entry>& pitEntry)
{
  Name interestName = pitEntry->getName();
  std::cout << "!! PIT EXPIRED: " << interestName.toUri()
            << " at " << std::fixed << std::setprecision(2) << ns3::Simulator::Now().GetSeconds() 
            << "s" << std::endl << std::flush;
  
  // Log details about the expired entry
  AggregatePitInfo* pitInfo = pitEntry->getStrategyInfo<AggregatePitInfo>();
  if (pitInfo) {
    std::cout << "  [Expired] " << pitInfo->pendingIds.size() << " pending IDs: { ";
    for (int id : pitInfo->pendingIds) {
      std::cout << id << " ";
    }
    std::cout << "}" << std::endl << std::flush;
  }
}

} // namespace fw
} // namespace nfd
