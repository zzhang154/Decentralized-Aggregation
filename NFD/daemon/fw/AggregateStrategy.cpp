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
  , m_forwarder(forwarder)
  , m_nodeId(ns3::NodeContainer::GetGlobal().Get(ns3::Simulator::GetContext())->GetId() + 1)
{
  // Set the instance name explicitly
  this->setInstanceName(name);

  // Determine node role and logical ID for logging
  uint32_t nodeIndex = m_nodeId - 1;
  m_nodeRole = ns3::ndn::AggregateUtils::determineNodeRole(nodeIndex);
  std::cout << ns3::ndn::AggregateUtils::getNodeRoleString(m_nodeRole, nodeIndex) 
            << " initialized AggregateStrategy" << std::endl;

  // Register for PIT expiration
  registerPitExpirationCallback();

  std::cout << "AggregateStrategy initialized for Forwarder." << std::endl;
  std::cout << "Strategy will use virtual method overrides." << std::endl << std::flush;
}

// Add these new methods:

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
  // 1. Log debug information
  logDebugInfo(interest, ingress);

  // 2. Check for interest aggregation (early return if aggregated)
  if (checkInterestAggregation(interest, ingress, pitEntry)) {
    return;
  }
          
  // 3. If not an aggregate Interest, use default behavior
  Name interestName = interest.getName();
  if (interestName.size() < 2 || interestName.get(0).toUri() != "aggregate") {
    forwardRegularInterest(interest, ingress, pitEntry);
    return;
  }

  // 4. Parse requested IDs and attach to PIT entry
  std::set<int> requestedIds = ns3::ndn::AggregateUtils::parseNumbersFromName(interestName);
  AggregatePitInfo* pitInfo = getAggregatePitInfo(pitEntry);
  pitInfo->neededIds = requestedIds;
  pitInfo->pendingIds = requestedIds;
  pitInfo->partialSum = 0;
  pitInfo->dependentInterests.clear();

  std::cout << ">> Received Interest " << interestName.toUri()
            << " from face " << ingress.face.getId() 
            << " requesting IDs = { ";
  for (int id : requestedIds) std::cout << id << " ";
  std::cout << "}" << std::endl << std::flush;

  // 5. Check Content Store for cached values
  if (processContentStoreHits(interest, ingress, pitEntry, pitInfo)) {
    return; // Fully satisfied from cache
  }

  // 6. Check for subset/superset relationships with existing interests
  checkSubsetSupersetRelationships(interest, pitEntry, pitInfo, requestedIds);

  // 7. Split and forward interests based on routing
  splitAndForwardInterests(interest, ingress, pitEntry, pitInfo);

  // 8. Set expiry timer
  this->setExpiryTimer(pitEntry, interest.getInterestLifetime());
}

// ** Handling incoming Data packets (from upstream) **
void 
AggregateStrategy::afterReceiveData(const Data& data, const FaceEndpoint& ingress,
                                   const shared_ptr<pit::Entry>& pitEntry) 
{
  // Add this explicit role logging at the start
  std::cout << ns3::ndn::AggregateUtils::getNodeRoleString(m_nodeRole, m_nodeId - 1) << " - STRATEGY processing Data: " << data.getName() 
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
  std::cout << "\n!! RAW DATA RECEIVED BY FORWARDER: " << ns3::ndn::AggregateUtils::getNodeRoleString(m_nodeRole, m_nodeId - 1)
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
  // 1. Find and validate the parent PIT entry
  auto [parentPit, parentInfo] = findParentPitEntry(dataName);
  if (!parentPit || !parentInfo) {
    return; // Invalid parent entry
  }

  // 2. Update parent with data from this sub-interest
  updateParentWithSubInterestData(data, dataName, parentInfo);
  
  // 3. If all components have arrived, satisfy the parent interest
  if (parentInfo->pendingIds.empty()) {
    // Send aggregated data to parent faces
    sendAggregatedDataToParentFaces(parentPit, parentInfo);
    
    // Process any piggybacked interests
    satisfyPiggybackedInterests(parentInfo);
    
    // IMPORTANT: Only remove the mapping AFTER we've finished using it
    m_parentMap.erase(dataName);
    std::cout << "  [SubInterest] Removed parent mapping for " << dataName.toUri() << std::endl << std::flush;
  }
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
      
      // In processWaitingInterestData, replace the code inside the "if (!stillWaitingForData)" block:

      if (!stillWaitingForData) {
        // Create and send the aggregated data
        Name childName = waitingPit->getName();
        auto childData = ns3::ndn::AggregateUtils::createDataWithValue(childName, waitingInfo->partialSum);
        
        // Ensure we identify the correct outgoing face by examining the original incoming face
        std::vector<Face*> outFaces;
        for (const auto& inRecord : waitingPit->getInRecords()) {
          outFaces.push_back(&inRecord.getFace());
        }
        
        // Send data directly to each face WITHOUT using PIT entries at all
        for (Face* outFace : outFaces) {
          try {
            // Use low-level direct send method that doesn't rely on PIT
            outFace->sendData(*childData);
            
            std::cout << "<< Sent aggregate Data for waiting Interest " << childName.toUri() 
                      << " with sum = " << waitingInfo->partialSum
                      << " to face " << outFace->getId() 
                      << " (direct send, bypassing PIT)" << std::endl;
          }
          catch (const std::exception& e) {
            std::cout << "  [ERROR] Failed to send waiting interest data: " << e.what() << std::endl;
          }
        }
        
        if (outFaces.empty()) {
          std::cout << "  [WARNING] Waiting interest has no InRecords - cannot send data" << std::endl;
        }
      }

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

// Helper function for "afterReceiveInterest":

void 
AggregateStrategy::logDebugInfo(const Interest& interest, const FaceEndpoint& ingress)
{
  std::cout << '\n' << ns3::ndn::AggregateUtils::getNodeRoleString(m_nodeRole, m_nodeId - 1)
          << " - STRATEGY received Interest: " << interest.getName() 
          << " via " << ingress.face.getId() 
          << " at " << std::fixed << std::setprecision(2) << ns3::Simulator::Now().GetSeconds() 
          << "s" << std::endl << std::flush;

  // Debug: Print PIT info
  printPitDebugInfo(m_forwarder.getPit());

  // Debug: Print FIB info
  Fib& fib = m_forwarder.getFib();
  std::cout << "DEBUG: FIB table has " << std::distance(fib.begin(), fib.end()) << " entries" << std::endl << std::flush;
  
  std::cout << "DEBUG: Current FIB entries:" << std::endl;
  for (const auto& fibEntry : fib) {
    std::cout << "  - Prefix: " << fibEntry.getPrefix() << " (Nexthops: " << fibEntry.getNextHops().size() << ")" << std::endl;
    for (const auto& nh : fibEntry.getNextHops()) {
      std::cout << "    * Face: " << nh.getFace().getId() << " Cost: " << nh.getCost() << std::endl;
    }
  }
}

void 
AggregateStrategy::printPitDebugInfo(const Pit& pit)
{
  std::cout << "Current PIT entries before forwarding Interest:" << std::endl;
  for (auto it = pit.begin(); it != pit.end(); ++it) {
    const nfd::pit::Entry &entry = *it;
    std::cout << "  PIT entry: " << entry.getName() 
              << " (InFaces=" << entry.getInRecords().size() 
              << ", OutFaces=" << entry.getOutRecords().size() << ")" << std::endl;
  }
}

bool 
AggregateStrategy::checkInterestAggregation(const Interest& interest, const FaceEndpoint& ingress,
                                          const shared_ptr<pit::Entry>& pitEntry)
{
  // Check #1: Interest has already been forwarded (has OutFaces)
  if (pitEntry->hasOutRecords()) {
    bool isSameFaceDuplicate = false;
    bool isDifferentFaceDuplicate = false;
    
    for (const auto& inRecord : pitEntry->getInRecords()) {
      if (inRecord.getFace().getId() == ingress.face.getId()) {
        isSameFaceDuplicate = true;
      } else {
        isDifferentFaceDuplicate = true;
      }
    }
    
    if (isSameFaceDuplicate) {
      std::cout << "  [Interest Aggregation] Duplicate interest from same face detected" << std::endl;
      std::cout << "  [Interest Aggregation] Interest " << interest.getName() 
                << " already forwarded - suppressing redundant forwarding" << std::endl;
      return true; // Aggregated
    }
    
    if (isDifferentFaceDuplicate) {
      std::cout << "  [Interest Aggregation] Duplicate interest from different face detected" << std::endl;
      std::cout << "  [Interest Aggregation] Interest " << interest.getName() 
                << " aggregated (added face " << ingress.face.getId() 
                << " to existing PIT entry)" << std::endl;
      return true; // Aggregated
    }
  }
  
  // Check #2: Another PIT entry exists with the same name and has been forwarded
  Pit &pit = m_forwarder.getPit();
  for (auto it = pit.begin(); it != pit.end(); ++it) {
    const nfd::pit::Entry &entry = *it;
    
    if (&entry == pitEntry.get()) {
      continue;
    }
    
    if (entry.getName() == interest.getName() && entry.hasOutRecords()) {
      std::cout << "  [Interest Aggregation] Duplicate interest " << interest.getName() 
                << " detected across different PIT entries" << std::endl;
      std::cout << "  [Interest Aggregation] Original PIT entry with "
                << entry.getInRecords().size() << " in-faces and "
                << entry.getOutRecords().size() << " out-faces" << std::endl;
      return true; // Aggregated
    }
  }
  
  return false; // Not aggregated
}

void 
AggregateStrategy::forwardRegularInterest(const Interest& interest, const FaceEndpoint& ingress,
                                        const shared_ptr<pit::Entry>& pitEntry)
{
  // Get the FIB entry
  const fib::Entry& fibEntry = this->lookupFib(*pitEntry);
  
  // Find a face to forward to (first available nexthop)
  const fib::NextHopList& nexthops = fibEntry.getNextHops();
  if (!nexthops.empty()) {
    Face& outFace = nexthops.begin()->getFace();
    std::cout << "[Strategy] Forwarding regular Interest " 
            << interest.getName() << " to face " << outFace.getId() << std::endl;
    this->sendInterest(interest, outFace, pitEntry);

    // CRITICAL FIX: Preserve the InRecord that NDN removes during forwarding
    pitEntry->insertOrUpdateInRecord(ingress.face, interest);
    std::cout << "  [PRESERVED] Restored InRecord for face " << ingress.face.getId() 
              << " in PIT entry for " << interest.getName() << std::endl;
  }
}

bool 
AggregateStrategy::processContentStoreHits(const Interest& interest, const FaceEndpoint& ingress,
                                        const shared_ptr<pit::Entry>& pitEntry, 
                                        AggregatePitInfo* pitInfo)
{
  // Check if we can satisfy from cache
  for (auto it = pitInfo->pendingIds.begin(); it != pitInfo->pendingIds.end();) {
    int id = *it;
    if (m_cachedValues.find(id) != m_cachedValues.end()) {
      uint64_t cachedValue = m_cachedValues[id];
      pitInfo->partialSum += cachedValue;
      std::cout << "  [CacheHit] Value for ID " << id << " = " 
                << cachedValue << " (from CS)" << std::endl << std::flush;
      it = pitInfo->pendingIds.erase(it);
      continue;
    }
    ++it;
  }

  // If all satisfied from cache, create Data packet
  if (pitInfo->pendingIds.empty()) {
    uint64_t totalSum = pitInfo->partialSum;
    auto data = ns3::ndn::AggregateUtils::createDataWithValue(interest.getName(), totalSum);
    
    for (const auto& inRecord : pitEntry->getInRecords()) {
        Face& outFace = inRecord.getFace();
        this->sendData(*data, outFace, pitEntry);
    }
    std::cout << "<< Satisfied Interest " << interest.getName().toUri() 
              << " from cache with sum = " << totalSum << std::endl << std::flush;
    return true; // Fully satisfied from cache
  }
  
  return false; // Not fully satisfied
}

void 
AggregateStrategy::checkSubsetSupersetRelationships(const Interest& interest, 
                                                  const shared_ptr<pit::Entry>& pitEntry,
                                                  AggregatePitInfo* pitInfo,
                                                  const std::set<int>& requestedIds)
{
  Name interestName = interest.getName();
  Pit& pitTable = m_forwarder.getPit();
  
  for (auto it = pitTable.begin(); it != pitTable.end(); ++it) {
    const pit::Entry& entryRef = *it;

    // Skip if not aggregate interest or same PIT entry
    Name existingName = entryRef.getName();
    if (existingName.size() < 2 || existingName.get(0).toUri() != "aggregate" || 
        &entryRef == pitEntry.get()) {
      continue;
    }

    // Use utility function to check sequence matching
    bool sequencesMatch = ns3::ndn::AggregateUtils::doSequenceComponentsMatch(existingName, interestName);
    if (!sequencesMatch) {
      continue;
    }

    // Get existing interest's IDs
    std::set<int> existingIds = ns3::ndn::AggregateUtils::parseNumbersFromName(existingName);

    // Check subset/superset relation
    bool existingIsSuperset = std::includes(existingIds.begin(), existingIds.end(),
                                          requestedIds.begin(), requestedIds.end());
    bool existingIsSubset = std::includes(requestedIds.begin(), requestedIds.end(),
                                        existingIds.begin(), existingIds.end());
                                        
    if (existingIsSuperset) {
      // Piggyback on existing interest
      std::cout << "  [Piggyback] Interest " << interestName.toUri() 
                << " piggybacks on superset Interest " << existingName.toUri() << std::endl << std::flush;
      AggregatePitInfo* supersetInfo = entryRef.getStrategyInfo<AggregatePitInfo>();
      if (supersetInfo) {
        supersetInfo->dependentInterests.push_back(pitEntry);
      }
      return; // Return, don't forward
    }
    else if (existingIsSubset) {
      std::cout << "  [Subset] Interest " << existingName.toUri() 
                << " is a subset of new Interest " << interestName.toUri() << std::endl << std::flush;
      
      // Create WaitInfo structure if needed
      if (!pitInfo->waitInfo) {
        pitInfo->waitInfo = std::make_shared<WaitInfo>();
      }
      
      // Track IDs coming from existing interest
      for (int overlapId : existingIds) {
        if (pitInfo->pendingIds.find(overlapId) != pitInfo->pendingIds.end()) {
          pitInfo->pendingIds.erase(overlapId);
          pitInfo->waitInfo->waitingFor[overlapId] = entryRef.getName();
          
          std::cout << "  [Tracking] ID " << overlapId << " will come from " 
                    << entryRef.getName().toUri() << std::endl << std::flush;
        }
      }
      
      // Link to wait for the subset's Data
      Name subsetDataName = entryRef.getName();
      m_waitingInterests[subsetDataName].push_back(pitEntry);
    }
  }
}

void 
AggregateStrategy::splitAndForwardInterests(const Interest& interest, const FaceEndpoint& ingress,
                                          const shared_ptr<pit::Entry>& pitEntry,
                                          AggregatePitInfo* pitInfo)
{
  // Skip if no pending IDs
  if (pitInfo->pendingIds.empty()) {
    std::cout << "  (No new sub-interests forwarded for " << interest.getName().toUri() << ")" << std::endl << std::flush;
    return;
  }

  // Group pending IDs by next-hop face (using FIB)
  std::map<Face*, std::vector<int>> faceToIdsMap;
  Fib& fib = m_forwarder.getFib();
  
  // Map IDs to faces
  for (int id : pitInfo->pendingIds) {
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

  // Optimization: All IDs to one face
  if (faceToIdsMap.size() == 1 && faceToIdsMap.begin()->second.size() == pitInfo->pendingIds.size()) {
    handleSingleFaceForwarding(interest, ingress, pitEntry, pitInfo, faceToIdsMap);
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

  // Create and forward split interests
  forwardSplitInterests(interest, ingress, pitEntry, faceToIdsMap);
}

void 
AggregateStrategy::handleSingleFaceForwarding(const Interest& interest, const FaceEndpoint& ingress,
                                           const shared_ptr<pit::Entry>& pitEntry,
                                           AggregatePitInfo* pitInfo,
                                           const std::map<Face*, std::vector<int>>& faceToIdsMap)
{
  Face* outFace = faceToIdsMap.begin()->first;
  std::cout << "OPTIMIZATION: All " << pitInfo->pendingIds.size() 
            << " IDs route to the same face (ID: " << outFace->getId() 
            << ")." << std::endl;
  
  // Check if original interest already has exactly what we need
  bool needsRewrite = false;
  std::set<int> originalInterestIds = ns3::ndn::AggregateUtils::parseNumbersFromName(interest.getName());
  if (originalInterestIds != pitInfo->pendingIds) {
    needsRewrite = true;
  }
  
  if (needsRewrite) {
    // Create optimized interest with only pending IDs
    std::vector<int> pendingIdsList(pitInfo->pendingIds.begin(), pitInfo->pendingIds.end());
    std::sort(pendingIdsList.begin(), pendingIdsList.end());
    
    Name optimizedName;
    optimizedName.append("aggregate");
    for (int id : pendingIdsList) {
      optimizedName.appendNumber(id);
    }

    // Preserve sequence number if present
    Name::Component seqComponent = ns3::ndn::AggregateUtils::extractSequenceComponent(interest.getName());
    if (!seqComponent.empty()) {
      optimizedName.append(seqComponent);
    }

    std::cout << "  >> Creating optimized interest with only pending IDs: " 
              << optimizedName << std::endl;
              
    // Create and forward the optimized interest
    auto optimizedInterest = ns3::ndn::AggregateUtils::createSplitInterest(
      optimizedName, interest.getInterestLifetime());

    // Insert into PIT and set up parent relationship
    auto newPitEntry = m_forwarder.getPit().insert(*optimizedInterest).first;
    AggregateSubInfo* subInfo = newPitEntry->insertStrategyInfo<AggregateSubInfo>().first;
    if (subInfo) {
      subInfo->parentEntry = pitEntry;
    }

    // Add to parent map
    m_parentMap[optimizedName] = pitEntry;

    // Send and preserve in-records
    this->sendInterest(*optimizedInterest, *outFace, newPitEntry);
    
    // Copy original InRecords
    for (const auto& inRecord : pitEntry->getInRecords()) {
      newPitEntry->insertOrUpdateInRecord(inRecord.getFace(), *optimizedInterest);
      std::cout << "  [PRESERVED] Copied InRecord from original PIT entry (face " 
                << inRecord.getFace().getId() << ") to optimized PIT entry" << std::endl;
    }

    // If no InRecords, use ingress face
    if (pitEntry->getInRecords().empty()) {
      newPitEntry->insertOrUpdateInRecord(ingress.face, *optimizedInterest);
      std::cout << "  [PRESERVED] Added ingress face " << ingress.face.getId() 
                << " as InRecord for optimized PIT entry" << std::endl;
    }
  } 
  else {
    // Forward original interest directly
    std::cout << "  >> Forwarding original interest directly - no optimization needed" << std::endl;
    this->sendInterest(interest, *outFace, pitEntry);

    // Restore InRecord
    pitEntry->insertOrUpdateInRecord(ingress.face, interest);
    std::cout << "  [PRESERVED] Restored InRecord for face " << ingress.face.getId() 
              << " in PIT entry for " << interest.getName() << std::endl;
  }
}

void 
AggregateStrategy::forwardSplitInterests(const Interest& interest, const FaceEndpoint& ingress,
                                      const shared_ptr<pit::Entry>& pitEntry,
                                      const std::map<Face*, std::vector<int>>& faceToIdsMap)
{
  // Create sub-interest for each group of IDs
  for (const auto& pair : faceToIdsMap) {
    Face* outFace = pair.first;
    const std::vector<int>& faceIds = pair.second;
    
    // Skip empty sets
    if (faceIds.empty()) continue;
    
    // Create a new Name with only the IDs for this face
    Name subInterestName;
    subInterestName.append("aggregate");
    for (int id : faceIds) {
      subInterestName.appendNumber(id);
    }
    
    // Preserve sequence number
    Name::Component seqComponent = ns3::ndn::AggregateUtils::extractSequenceComponent(interest.getName());
    if (!seqComponent.empty()) {
      subInterestName.append(seqComponent);
    }
    
    std::cout << "  >> Creating sub-interest for " << faceIds.size() 
              << " IDs: " << subInterestName << " (face " << outFace->getId() << ")" << std::endl;
    
    // Create a new Interest with this name
    auto subInterest = ns3::ndn::AggregateUtils::createSplitInterest(
      subInterestName, interest.getInterestLifetime());
    
    // Insert into PIT
    auto newPitEntry = m_forwarder.getPit().insert(*subInterest).first;
    
    // Add metadata to link this sub-interest with its parent
    AggregateSubInfo* subInfo = newPitEntry->insertStrategyInfo<AggregateSubInfo>().first;
    if (subInfo) {
      subInfo->parentEntry = pitEntry;
    }
    
    // Also add to our parent map
    m_parentMap[subInterestName] = pitEntry;
    
    // Forward the interest
    this->sendInterest(*subInterest, *outFace, newPitEntry);
    
    // Ensure the sub-interest has the right InRecords copied
    newPitEntry->insertOrUpdateInRecord(ingress.face, *subInterest);
    
    std::cout << "  [Sub-Interest] Forwarded Interest " << subInterestName.toUri() 
              << " via face " << outFace->getId() << std::endl << std::flush;
  }
}

// Debug helper functions for "processSubInterestData"
std::pair<std::shared_ptr<pit::Entry>, AggregateStrategy::AggregatePitInfo*> 
AggregateStrategy::findParentPitEntry(const Name& dataName)
{
  auto parentIt = m_parentMap.find(dataName);
  if (parentIt == m_parentMap.end()) {
    return {nullptr, nullptr}; // Not a sub-interest
  }

  std::cout << "  [SubInterest] Found matching parent for Data " << dataName.toUri() << std::endl << std::flush;
  
  // Retrieve the parent PIT entry that initiated this sub-interest
  std::shared_ptr<pit::Entry> parentPit = parentIt->second.lock();
  if (!parentPit) {
    std::cout << "  [SubInterest] Parent PIT entry already expired" << std::endl << std::flush;
    m_parentMap.erase(dataName);
    return {nullptr, nullptr};
  }
  
  AggregatePitInfo* parentInfo = parentPit->getStrategyInfo<AggregatePitInfo>();
  if (!parentInfo) {
    std::cout << "  [SubInterest] No strategy info found for parent PIT entry" << std::endl << std::flush;
    m_parentMap.erase(dataName);
    return {nullptr, nullptr};
  }
  
  std::cout << "  [SubInterest] Processing Data for parent Interest " << parentPit->getName().toUri() << std::endl << std::flush;
  return {parentPit, parentInfo};
}

uint64_t 
AggregateStrategy::updateParentWithSubInterestData(const Data& data, const Name& dataName, 
                                                AggregatePitInfo* parentInfo)
{
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
            << value << " to parent Interest " << parentInfo->partialSum << std::endl;
  std::cout << "    Remaining IDs for parent: { ";
  for (int pid : parentInfo->pendingIds) std::cout << pid << " ";
  std::cout << "}" << std::endl << std::flush;
  
  return value;
}

std::vector<Face*> 
AggregateStrategy::extractFacesFromPitEntry(const std::shared_ptr<pit::Entry>& pitEntry)
{
  std::vector<Face*> outFaces;
  for (const auto& inRecord : pitEntry->getInRecords()) {
    outFaces.push_back(&inRecord.getFace());
  }
  
  if (outFaces.empty()) {
    std::cout << "  [WARNING] PIT entry has no InRecords - cannot send data" << std::endl;
  }
  
  return outFaces;
}

void 
AggregateStrategy::sendDataDirectly(const std::shared_ptr<::ndn::Data>& data, Face* outFace, 
                                 const Name& dataName, uint64_t value)
{
  try {
    // Use low-level direct send method that doesn't rely on PIT
    outFace->sendData(*data);
    
    std::cout << "<< Sent aggregate Data " << dataName.toUri() 
              << " with sum = " << value 
              << " to face " << outFace->getId() 
              << " (direct send, bypassing PIT)" << std::endl << std::flush;
  }
  catch (const std::exception& e) {
    std::cout << "  [ERROR] Failed to send data: " << e.what() << std::endl;
  }
}

void 
AggregateStrategy::sendAggregatedDataToParentFaces(std::shared_ptr<pit::Entry> parentPit,
                                                AggregatePitInfo* parentInfo)
{
  std::cout << "  [SubInterest] All components received, creating final aggregated Data" << std::endl << std::flush;
  uint64_t totalSum = parentInfo->partialSum;
  Name parentName = parentPit->getName();
  
  // Create the aggregated data packet
  auto aggData = ns3::ndn::AggregateUtils::createDataWithValue(parentName, totalSum);
  
  try {
    // Extract faces while parent PIT entry is still valid
    std::vector<Face*> outFaces = extractFacesFromPitEntry(parentPit);
    
    // Send data directly to each face WITHOUT using PIT entries at all
    for (Face* outFace : outFaces) {
      sendDataDirectly(aggData, outFace, parentName, totalSum);
    }
  }
  catch (const std::exception& e) {
    std::cout << "  [ERROR] Failed to process parent PIT: " << e.what() << std::endl;
  }
}

void 
AggregateStrategy::satisfyPiggybackedInterests(AggregatePitInfo* parentInfo)
{
  if (parentInfo->dependentInterests.empty()) {
    return;
  }
  
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
    std::vector<Face*> childFaces = extractFacesFromPitEntry(childPit);
    if (childFaces.empty()) continue;
    
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

} // namespace fw
} // namespace nfd
