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

#include "ns3/ndnSIM/utils/ndn-aggregate-utils.hpp"

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

// Add this new method for processing data for aggregation:
void
AggregateStrategy::processDataForAggregation(const Data& data, const FaceEndpoint& ingress,
                                           const shared_ptr<pit::Entry>& pitEntry)
{
  // Check PIT entry state before NDN clears it
  std::cout << "  PROCESSING DATA: " << data.getName()
            << " for PIT entry: " << pitEntry->getName()
            << " (InFaces=" << pitEntry->getInRecords().size()
            << ", OutFaces=" << pitEntry->getOutRecords().size() << ")" << std::endl;
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

      // CRITICAL FIX: Preserve the InRecord that NDN removes during forwarding
      pitEntry->insertOrUpdateInRecord(ingress.face, interest);
      std::cout << "  [PRESERVED] Restored InRecord for face " << ingress.face.getId() 
                << " in PIT entry for " << interest.getName() << std::endl;
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

    // Extract sequence components for comparison
    Name::Component existingSeqComponent, newSeqComponent;
    for (size_t i = 0; i < existingName.size(); i++) {
      if (existingName[i].toUri().find("seq=") != std::string::npos) {
        existingSeqComponent = existingName[i];
        break;
      }
    }
    for (size_t i = 0; i < interestName.size(); i++) {
      if (interestName[i].toUri().find("seq=") != std::string::npos) {
        newSeqComponent = interestName[i];
        break;
      }
    }

    // Only consider subset/superset if sequence numbers match
    bool sequencesMatch = (existingSeqComponent == newSeqComponent) || 
                          (existingSeqComponent.empty() && newSeqComponent.empty());
                          
    if (!sequencesMatch) {
      continue; // Skip this entry if the sequence components don't match
    }

    // Get the set of IDs for the existing pending interest - with improved parsing
    std::set<int> existingIds;
    for (size_t i = 1; i < existingName.size(); ++i) {
      // Skip sequence number components
      if (existingName[i].toUri().find("seq=") != std::string::npos) {
        continue;
      }
      
      if (existingName[i].isNumber()) {
        existingIds.insert(existingName[i].toNumber());
      } else {
        try {
          std::string component = existingName[i].toUri();
          // Parse the %02 format
          if (component.length() > 1 && component[0] == '%') {
            component = component.substr(1);
            existingIds.insert(std::stoi(component));
          }
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
      std::cout << "  [Subset] Interest " << existingName.toUri() 
                << " is a subset of new Interest " << interestName.toUri() << std::endl << std::flush;
      
      // NEW: Create a WaitInfo structure to track dependencies
      if (!pitInfo->waitInfo) {
        pitInfo->waitInfo = std::make_shared<WaitInfo>();
      }
      
      // Track which IDs are coming from which interests
      for (int overlapId : existingIds) {
        if (pitInfo->pendingIds.find(overlapId) != pitInfo->pendingIds.end()) {
          // Remove from pendingIds, but add to waitingFor map
          pitInfo->pendingIds.erase(overlapId);
          pitInfo->waitInfo->waitingFor[overlapId] = entryRef.getName();
          
          std::cout << "  [Tracking] ID " << overlapId << " will come from " 
                    << entryRef.getName().toUri() << std::endl << std::flush;
        }
      }
      
      // Link new interest to wait for the subset's Data
      Name subsetDataName = entryRef.getName();
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
      
      // Check if the original interest already has exactly the IDs we need
      bool needsRewrite = false;
      std::set<int> originalInterestIds = parseRequestedIds(interest);
      if (originalInterestIds != pitInfo->pendingIds) {
        needsRewrite = true;
      }
      
      if (needsRewrite) {
        // Original code continues below - create a new interest with optimized IDs
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

        // CRITICAL FIX: Since this is a new PIT entry, copy the original InRecords
        for (const auto& inRecord : pitEntry->getInRecords()) {
          newPitEntry->insertOrUpdateInRecord(inRecord.getFace(), *optimizedInterest);
          std::cout << "  [PRESERVED] Copied InRecord from original PIT entry (face " 
                    << inRecord.getFace().getId() << ") to optimized PIT entry" << std::endl;
        }

        // If there were no InRecords in the original, use the ingress face
        if (pitEntry->getInRecords().empty()) {
          newPitEntry->insertOrUpdateInRecord(ingress.face, *optimizedInterest);
          std::cout << "  [PRESERVED] Added ingress face " << ingress.face.getId() 
                    << " as InRecord for optimized PIT entry" << std::endl;
        }

      } 
      else {
        // The original interest already has exactly what we need, just forward it directly
        std::cout << "  >> Forwarding original interest directly - no optimization needed" << std::endl;
        this->sendInterest(interest, *outFace, pitEntry);

        // CRITICAL FIX: Restore the InRecord that NDN removed during forwarding
        pitEntry->insertOrUpdateInRecord(ingress.face, interest);
        std::cout << "  [PRESERVED] Restored InRecord for face " << ingress.face.getId() 
                  << " in PIT entry for " << interest.getName() << std::endl;
      }

      this->setExpiryTimer(pitEntry, interest.getInterestLifetime());
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

      // Add this block to preserve sequence numbers in split interests
      // Preserve sequence number from original interest
      for (size_t i = 0; i < interest.getName().size(); i++) {
        if (interest.getName()[i].toUri().find("seq=") != std::string::npos) {
          subInterestName.append(interest.getName()[i]);
          std::cout << "  >> Preserved sequence component in split interest: " 
                    << interest.getName()[i].toUri() << std::endl;
          break;
        }
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

      // CRITICAL FIX: Add an InRecord to the new PIT entry, using the original ingress face
      temporaryEntry->insertOrUpdateInRecord(ingress.face, *subInterest);
      std::cout << "  [PRESERVED] Added InRecord from original face " << ingress.face.getId()
                << " to sub-interest PIT entry for " << subInterestName << std::endl;

      // After forwarding, find the newly created PIT entry and attach our metadata
      shared_ptr<pit::Entry> subPitEntry = m_forwarder.getPit().find(*subInterest);

      if (subPitEntry) {
        // Store parent-child relationship as before
        AggregateSubInfo* subInfo = subPitEntry->insertStrategyInfo<AggregateSubInfo>().first;
        if (subInfo) {
            subInfo->parentEntry = pitEntry;
        }
        // **Add dummy in-record so Data triggers strategy**
        if (subPitEntry->getInRecords().empty()) {
          Face& dummyFace = *outFace;  // use the face from which the aggregate Interest arrived
            subPitEntry->insertOrUpdateInRecord(dummyFace, *subInterest);
            std::cout << "  (Added dummy in-record for sub-interest " 
                      << subInterestName.toUri() << " on face " << dummyFace.getId() 
                      << ")" << std::endl;
        }
    
        std::cout << "  >> Forwarded sub-Interest " << subInterestName.toUri()
                  << " to face " << outFace->getId() << " (new PIT entry created)" << std::endl;
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
  return;
}

// ** Handling incoming Data packets (from upstream) **
void 
AggregateStrategy::afterReceiveData(const Data& data, const FaceEndpoint& ingress,
                                   const shared_ptr<pit::Entry>& pitEntry) 
{
  // Add this explicit role logging at the start
  std::cout << getNodeRoleString() << " - STRATEGY processing Data: " << data.getName() 
            << " from face " << ingress.face.getId() 
            << " at " << std::fixed << std::setprecision(2) << ns3::Simulator::Now().GetSeconds() 
            << "s" << std::endl << std::flush;
            
  // Immediately after the above, add this to trace data forwarding:
  std::cout << "  Current PIT entry has " << pitEntry->getInRecords().size() 
            << " in-faces and " << pitEntry->getOutRecords().size() 
            << " out-faces" << std::endl;
            
  // Continue with existing code
  Strategy::afterReceiveData(data, ingress, pitEntry);

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

  // Process data using our modular approach
  processSubInterestData(data, dataName, ingress, pitEntry);
  processWaitingInterestData(data, dataName, ingress, pitEntry);
  processDirectData(data, dataName, ingress, pitEntry);

  // ** Forward the Data down to any PIT downstreams as usual (if not already handled) **
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

// Add this method to AggregateStrategy class - it's called before the forwarder processes any data
void
AggregateStrategy::beforeSatisfyInterest(const Data& data,
                                     const FaceEndpoint& ingress,
                                     const shared_ptr<pit::Entry>& pitEntry)
{
  std::cout << "\n!! RAW DATA RECEIVED BY FORWARDER: " << getNodeRoleString() 
            << " received data " << data.getName() 
            << " from face " << ingress.face.getId() << std::endl;
  
  // Print PIT entry state BEFORE it gets cleared by the forwarder
  std::cout << "  PIT ENTRY BEFORE SATISFACTION: " << pitEntry->getName()
            << " (InFaces=" << pitEntry->getInRecords().size()
            << ", OutFaces=" << pitEntry->getOutRecords().size() << ")" << std::endl;
            
  // Print all InFaces so we know where data should go
  std::cout << "  InFaces:";
  for (const auto& inRecord : pitEntry->getInRecords()) {
    std::cout << " " << inRecord.getFace().getId();
  }
  std::cout << std::endl;
  
  // *** ADD DATA AGGREGATION LOGIC HERE, BEFORE PIT RECORDS ARE CLEARED ***
  Name dataName = data.getName();
  std::cout << "<< [beforeSatisfyInterest] Processing data: " << dataName.toUri() 
            << " from face " << ingress.face.getId() << std::endl << std::flush;
  
  // Check if this data should be consumed by the strategy
  bool isSubInterestResponse = (m_parentMap.find(dataName) != m_parentMap.end());
  bool hasWaitingInterests = (m_waitingInterests.find(dataName) != m_waitingInterests.end());
  
  if (isSubInterestResponse || hasWaitingInterests) {
    std::cout << "  [Consume] Data " << dataName.toUri() 
              << " is being handled by the strategy - suppressing forwarding" << std::endl;
    
    // Process data using our modular approach - BEFORE PIT records are cleared
    processSubInterestData(data, dataName, ingress, pitEntry);
    processWaitingInterestData(data, dataName, ingress, pitEntry);
    processDirectData(data, dataName, ingress, pitEntry);
    
    // For completely consumed data, block regular NDN forwarding by clearing all in-records
    while (!pitEntry->getInRecords().empty()) {
      const pit::InRecord& inRecord = pitEntry->getInRecords().front();
      pitEntry->deleteInRecord(inRecord.getFace());
    }
    
    // CRITICAL: Don't call the base class method for consumed data
    // This completely prevents forwarding by the forwarder
    return;
  } 
  else {
    // For normal data that needs to flow downstream, let the forwarder handle it
    std::cout << "  [Forward] Data " << dataName.toUri() 
              << " will be forwarded downstream by forwarder" << std::endl;
    
    // Process data using our modular approach before base class handling
    processSubInterestData(data, dataName, ingress, pitEntry);
    processWaitingInterestData(data, dataName, ingress, pitEntry);
    processDirectData(data, dataName, ingress, pitEntry);
    
    // Only call base implementation for non-consumed data
    Strategy::beforeSatisfyInterest(data, ingress, pitEntry);
  }
}

// Add these near the other helper methods:

void 
AggregateStrategy::processSubInterestData(const Data& data, const Name& dataName, 
                                        const FaceEndpoint& ingress,
                                        const shared_ptr<pit::Entry>& pitEntry)
{
  auto parentIt = m_parentMap.find(dataName);
  if (parentIt == m_parentMap.end()) {
    return; // Not a sub-interest
  }

  std::cout << "  [SubInterest] Found matching parent for Data " << dataName.toUri() << std::endl << std::flush;
  // This data is a response to a sub-interest that our strategy created.
  // Retrieve the parent PIT entry that initiated this sub-interest
  std::shared_ptr<pit::Entry> parentPit = parentIt->second.lock();
  if (!parentPit) {
    std::cout << "  [SubInterest] Parent PIT entry already expired" << std::endl << std::flush;
    m_parentMap.erase(dataName);
    return;
  }
  
  AggregatePitInfo* parentInfo = parentPit->getStrategyInfo<AggregatePitInfo>();
  if (!parentInfo) {
    std::cout << "  [SubInterest] No strategy info found for parent PIT entry" << std::endl << std::flush;
    m_parentMap.erase(dataName);
    return;
  }
  
  std::cout << "  [SubInterest] Processing Data for parent Interest " << parentPit->getName().toUri() << std::endl << std::flush;
  
  // Parse the content of the Data to extract the numeric value
  uint64_t value = ns3::ndn::AggregateUtils::extractValueFromContent(data);
  
  // Determine which IDs this data covers (from its name components)
  std::set<int> dataIds = ns3::ndn::AggregateUtils::parseNumbersFromName(dataName);
  
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
    
    auto aggData = ns3::ndn::AggregateUtils::createDataWithValue(parentName, totalSum);
    
    // SAFE APPROACH: Create temporary interest and PIT entry for sending
    // Capture face information before potential PIT entry invalidation
    std::vector<Face*> outFaces;
    for (const auto& inRecord : parentPit->getInRecords()) {
      outFaces.push_back(&inRecord.getFace());
    }
    
    if (outFaces.empty()) {
      std::cout << "  [WARNING] Parent PIT entry has no InRecords - cannot send data" << std::endl;
    } else {
      // Process each face with a robust approach
      for (Face* outFace : outFaces) {
        try {
          // Create a temporary interest with the same name as the parent
          Interest tempInterest(parentName);
          auto tempPitEntry = m_forwarder.getPit().insert(tempInterest).first;
          
          // Add the face as an in-record for our temporary PIT entry
          tempPitEntry->insertOrUpdateInRecord(*outFace, tempInterest);
          
          // Now send data using this temporary PIT entry
          this->sendData(*aggData, *outFace, tempPitEntry);
          
          std::cout << "<< Generated aggregate Data " << parentName.toUri() 
                   << " with sum = " << totalSum 
                   << " to face " << outFace->getId() 
                   << " (via safe temp PIT entry)" << std::endl << std::flush;
        }
        catch (const std::exception& e) {
          std::cout << "  [ERROR] Failed to send data: " << e.what() << std::endl;
        }
      }
    }
    
    // Process any piggybacked interests with the same safe approach
    if (!parentInfo->dependentInterests.empty()) {
      std::cout << "  [SubInterest] Satisfying " << parentInfo->dependentInterests.size() 
                << " piggybacked child interests" << std::endl << std::flush;
                
      for (auto& weakChildPit : parentInfo->dependentInterests) {
        auto childPit = weakChildPit.lock();
        if (!childPit) continue;
        
        // Compute the subset sum for the child (it might be different from totalSum if parent had extra IDs)
        AggregatePitInfo* childInfo = childPit->getStrategyInfo<AggregatePitInfo>();
        if (!childInfo) continue;
        
        // Sum up values for child's needed IDs from our cached values (all should be available now)
        uint64_t childSum = 0;
        for (int cid : childInfo->neededIds) {
          if (m_cachedValues.find(cid) != m_cachedValues.end()) {
            childSum += m_cachedValues[cid];
          }
        }
        
        // Capture child's faces before potential PIT invalidation
        std::vector<Face*> childFaces;
        for (const auto& inRecord : childPit->getInRecords()) {
          childFaces.push_back(&inRecord.getFace());
        }
        
        if (childFaces.empty()) {
          std::cout << "  [WARNING] Child PIT entry has no InRecords - cannot send data" << std::endl;
          continue;
        }
        
        // Send Data to satisfy the child interest with safe approach
        Name childName = childPit->getName();
        auto childData = ns3::ndn::AggregateUtils::createDataWithValue(childName, childSum);
        
        // Send to all child's in-faces using temp PIT entries
        for (Face* outFace : childFaces) {
          try {
            // Create a temporary interest with the same name as the child
            Interest tempInterest(childName);
            auto tempPitEntry = m_forwarder.getPit().insert(tempInterest).first;
            
            // Add the face as an in-record for our temporary PIT entry
            tempPitEntry->insertOrUpdateInRecord(*outFace, tempInterest);
            
            // Now send data using this temporary PIT entry
            this->sendData(*childData, *outFace, tempPitEntry);
            
            std::cout << "<< Satisfied piggybacked Interest " << childName.toUri() 
                      << " with sum = " << childSum 
                      << " to face " << outFace->getId()
                      << " (via safe temp PIT entry)" << std::endl;
          }
          catch (const std::exception& e) {
            std::cout << "  [ERROR] Failed to send piggybacked data: " << e.what() << std::endl;
          }
        }
      }
    }
  }
  
  // Remove the mapping as this sub-interest's data has been processed
  m_parentMap.erase(dataName);
}

void 
AggregateStrategy::processWaitingInterestData(const Data& data, const Name& dataName, 
                                           const FaceEndpoint& ingress,
                                           const shared_ptr<pit::Entry>& pitEntry)
{
  auto waitIt = m_waitingInterests.find(dataName);
  if (waitIt == m_waitingInterests.end()) {
    return; // No waiting interests
  }
  
  std::cout << "  [WaitingInterest] Found " << waitIt->second.size() 
            << " interests waiting for Data " << dataName.toUri() << std::endl << std::flush;
            
  // Extract value from data
  uint64_t value = ns3::ndn::AggregateUtils::extractValueFromContent(data);
  
  // Extract IDs covered by this data
  std::set<int> dataIds = ns3::ndn::AggregateUtils::parseNumbersFromName(dataName);
  
  // Process each waiting interest IMMEDIATELY (not storing for later)
  for (auto& weakPit : waitIt->second) {
    auto waitingPit = weakPit.lock();
    if (!waitingPit) continue;
    
    AggregatePitInfo* waitingInfo = waitingPit->getStrategyInfo<AggregatePitInfo>();
    if (!waitingInfo) continue;
    
    // Update waiting interest's state with new data
    waitingInfo->partialSum += value;
    for (int gotId : dataIds) {
      waitingInfo->pendingIds.erase(gotId);
      
      // CRITICAL FIX: Also remove this ID from the waitingFor map if present
      if (waitingInfo->waitInfo) {
        // Check if this exact data name was what we were waiting for
        auto it = waitingInfo->waitInfo->waitingFor.begin();
        while (it != waitingInfo->waitInfo->waitingFor.end()) {
          if (it->second == dataName) {
            std::cout << "    [Tracking] Removed ID " << it->first 
                      << " from waiting list (data has arrived)" << std::endl;
            it = waitingInfo->waitInfo->waitingFor.erase(it);
          } else {
            ++it;
          }
        }
      }
    }
    
    std::cout << "    [Piggyback] Data " << dataName.toUri() << " received for waiting Interest " 
              << waitingPit->getName().toUri() << std::endl;
    
    // Check if waiting interest is REALLY ready - log and debug it!
    std::cout << "    [Debug] Waiting interest has " << waitingInfo->pendingIds.size() 
              << " remaining IDs: { ";
    for (int id : waitingInfo->pendingIds) std::cout << id << " ";
    std::cout << "}" << std::endl << std::flush;
    
    // If that waiting interest now has no pending IDs left, check if we have all needed data
    if (waitingInfo->pendingIds.empty()) {
      // Check if we're still waiting for data from other interests
      bool stillWaitingForData = false;
      
      if (waitingInfo->waitInfo) {
        // Print waiting IDs for debugging
        std::cout << "  [WaitingMap] Interest is waiting for " 
                  << waitingInfo->waitInfo->waitingFor.size() << " IDs from other interests: { ";
        for (const auto& pair : waitingInfo->waitInfo->waitingFor) {
          std::cout << pair.first << " (from " << pair.second.toUri() << ") ";
          stillWaitingForData = true;
        }
        std::cout << "}" << std::endl << std::flush;
      }
      
      if (stillWaitingForData) {
        std::cout << "  [WaitingInterest] Interest has empty pendingIds but is still waiting for data from other interests" 
                  << std::endl << std::flush;
        continue;
      }
      
      // Only proceed if we have all data (no pendingIds and no waitingFor entries)
      std::cout << "  [WaitingInterest] All components received for waiting interest, creating final Data" 
                << std::endl << std::flush;
      
      // ... rest of the existing code to create and send data ...
    }
    else {
      std::cout << "  [WaitingInterest] Interest still waiting for " << waitingInfo->pendingIds.size() 
                << " more IDs" << std::endl << std::flush;
    }
  }
  
  // Remove this entry from waiting list after processing all interests
  m_waitingInterests.erase(waitIt);
}

void
AggregateStrategy::processDirectData(const Data& data, const Name& dataName, 
                                   const FaceEndpoint& ingress,
                                   const shared_ptr<pit::Entry>& pitEntry)
{
  // Only process if this is not a sub-interest response
  if (m_parentMap.find(dataName) != m_parentMap.end()) {
    return;
  }
  
  std::cout << "  [DirectData] Processing regular Data packet (not sub-interest)" << std::endl << std::flush;
  
  // If dataName is atomic, cache its value (form "/aggregate/<id>")
  if (dataName.size() == 2) {
    std::cout << "  [DirectData] Processing atomic data for single ID" << std::endl << std::flush;
    
    try {
      int id = std::stoi(dataName.get(1).toUri());
      uint64_t val = ns3::ndn::AggregateUtils::extractValueFromContent(data);
      
      // Store in cache
      m_cachedValues[id] = val;
      std::cout << "  [CacheStore] Cached value for ID " << id << " = " << val << std::endl << std::flush;
    } 
    catch (...) { 
      std::cout << "  [DirectData] Failed to parse ID as integer" << std::endl << std::flush;
    }
  }
}

} // namespace fw
} // namespace nfd
