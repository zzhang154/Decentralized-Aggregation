// #include "ndn-aggregator.hpp"
// #include "ns3/log.h"
// #include "ns3/string.h"          // For StringValue
// #include "ns3/uinteger.h"        // For UintegerValue, MakeUintegerAccessor, MakeUintegerChecker
// #include "ns3/nstime.h"          // For TimeValue, MakeTimeAccessor, MakeTimeChecker (not time.h)
// #include "ns3/ndnSIM/helper/ndn-fib-helper.hpp"
// #include <ndn-cxx/encoding/buffer.hpp>
// #include <cstdlib>  // for rand()

// NS_LOG_COMPONENT_DEFINE("ndn.Aggregator");

// namespace ns3 {
// namespace ndn {

// // NS_OBJECT_ENSURE_REGISTERED(Aggregator);
// NS_OBJECT_ENSURE_REGISTERED(Aggregator);

// TypeId
// Aggregator::GetTypeId()
// {
//   static TypeId tid = TypeId("ns3::ndn::Aggregator")
//     .SetParent<ndn::App>()
//     .AddConstructor<Aggregator>()
//     .AddAttribute("Prefix", "Prefix of aggregated data served by this app",
//                   StringValue("/"), MakeNameAccessor(&Aggregator::m_prefix),
//                   MakeNameChecker())
//     .AddAttribute("ProducerCount", "Number of producer data sources to aggregate",
//                   UintegerValue(0), MakeUintegerAccessor(&Aggregator::m_producerCount),
//                   MakeUintegerChecker<uint32_t>())
//     .AddAttribute("Freshness", "Freshness of aggregated Data (0 means no cache control)",
//                   TimeValue(Seconds(1.0)), MakeTimeAccessor(&Aggregator::m_freshness),
//                   MakeTimeChecker());
//   return tid;
// }

// void
// Aggregator::StartApplication()
// {
//   // Initialize base NDN App (includes connecting face and PIT)
//   ndn::App::StartApplication();

//   // Register the aggregation prefix with local NDN FIB (so this app will receive Interests)
//   if (!m_prefix.empty()) {
//     FibHelper::AddRoute(GetNode(), m_prefix, m_face, 0);
//   }
//   m_seq = 0;
// }

// void
// Aggregator::StopApplication()
// {
//   // Cleanup pending state if any
//   m_pending.clear();
//   ndn::App::StopApplication();
// }

// void
// Aggregator::OnInterest(std::shared_ptr<const ndn::Interest> interest)
// {
//   // Log and trace incoming Interest
//   ndn::App::OnInterest(interest);
//   NS_LOG_INFO("Aggregator received Interest: " << interest->getName());
  
//   if (!m_active) { // check if application is active
//     return;
//   }
//   if (m_producerCount == 0) {
//     NS_LOG_WARN("ProducerCount is 0; no aggregation will be performed.");
//     return;
//   }

//   // Create a unique request identifier (string) for this aggregation cycle
//   std::string reqId = std::to_string(m_seq++);
//   PendingRequest pending;
//   pending.origName = interest->getName();
//   pending.expectedResponses = m_producerCount;
//   pending.receivedResponses = 0;
//   pending.totalSize = 0;
//   m_pending[reqId] = pending;

//   // Send sub-Interests to each producer
//   for (uint32_t i = 1; i <= m_producerCount; ++i) {
//     // Sub-interest Name: use the base prefix plus producer ID and request ID
//     ndn::Name subName = m_prefix;
//     subName.append(std::to_string(i));    // e.g., /agg/podX/1, /agg/podX/2, ...
//     subName.append(reqId);               // append unique request ID component

//     auto subInterest = std::make_shared<ndn::Interest>(subName);
//     subInterest->setInterestLifetime(ndn::time::seconds(2));
//     subInterest->setNonce(rand()); // random nonce for uniqueness

//     NS_LOG_INFO("Aggregator forwarding sub-Interest: " << subInterest->getName());
//     m_transmittedInterests(subInterest, this, m_face);
//     m_appLink->onReceiveInterest(*subInterest);  // send the Interest into NDN stack
//   }
// }

// void
// Aggregator::OnData(std::shared_ptr<const ndn::Data> data)
// {
//   NS_LOG_INFO("Aggregator received Data: " << data->getName());
  
//   // Identify which pending request this Data belongs to using the request ID component
//   ndn::Name dataName = data->getName();
//   if (dataName.size() < 1) {
//     return;
//   }
//   // Last component of Data name is the request ID we appended
//   std::string reqId = dataName.get(dataName.size() - 1).toUri();
//   auto it = m_pending.find(reqId);
//   if (it == m_pending.end()) {
//     NS_LOG_WARN("No pending aggregation for received Data with request ID=" << reqId);
//     return;
//   }

//   // Update the pending request with this Data
//   it->second.receivedResponses++;
//   it->second.totalSize += data->getContent().value_size();

//   // If all expected Data packets have been received, aggregate and reply
//   if (it->second.receivedResponses >= it->second.expectedResponses) {
//     // Prepare aggregated Data packet
//     ndn::Name dataName = it->second.origName;  // Use original Interest name as Data name
//     auto aggregatedData = std::make_shared<ndn::Data>(dataName);
    
//     // Create aggregated content (e.g., summarizing total size or combining contents)
//     std::string contentStr = "Aggregated " + std::to_string(it->second.receivedResponses) +
//                              " results, total content " + std::to_string(it->second.totalSize) + " bytes.";
//     auto contentBuf = std::make_shared< ::ndn::Buffer>(contentStr.size());
//     std::copy(contentStr.begin(), contentStr.end(), contentBuf->begin());
//     aggregatedData->setContent(contentBuf);

//     // Set freshness period for caching (if configured)
//     if (m_freshness.GetSeconds() > 0) {
//       aggregatedData->setFreshnessPeriod(ndn::time::milliseconds(m_freshness.GetMilliSeconds()));
//     }

//     // Use a dummy signature (required by NDN Data packets)
//     ::ndn::KeyLocator keyLocator;
//     ::ndn::SignatureInfo signatureInfo(static_cast<::ndn::tlv::SignatureTypeValue>(255));
//     aggregatedData->setSignatureInfo(signatureInfo);
    
//     // Create a zero-valued signature
//     static const uint8_t emptyBuffer[] = {0, 0, 0, 0};
//     ::ndn::Buffer signatureValue(emptyBuffer, 4);
//     aggregatedData->setSignatureValue(signatureValue);

//     NS_LOG_INFO("Aggregator replying with aggregated Data: " << aggregatedData->getName());
//     m_transmittedDatas(aggregatedData, this, m_face);
//     m_appLink->onReceiveData(*aggregatedData);  // send Data back towards consumer

//     // Remove the completed pending entry
//     m_pending.erase(it);
//   }
// }

// } // namespace ndn
// } // namespace ns3
