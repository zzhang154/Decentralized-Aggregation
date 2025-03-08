#ifndef NDN_AGGREGATOR_HPP
#define NDN_AGGREGATOR_HPP

#include "ndn-app.hpp"
#include <ndn-cxx/name.hpp>
#include <ndn-cxx/interest.hpp>
#include <ndn-cxx/data.hpp>
#include <map>
#include <string>

namespace ns3 {
namespace ndn {

class Aggregator : public App {
public:
  static TypeId GetTypeId();
  // Called when an Interest for the aggregator's prefix is received
  virtual void OnInterest(std::shared_ptr<const ndn::Interest> interest) override;
  // Called when a Data packet (from a producer) is received
  virtual void OnData(std::shared_ptr<const ndn::Data> data) override;

protected:
  virtual void StartApplication() override;
  virtual void StopApplication() override;

private:
  // Structure to keep track of an ongoing aggregation request
  struct PendingRequest {
    ndn::Name origName;        // Original Interest Name
    uint32_t expectedResponses; 
    uint32_t receivedResponses;
    size_t totalSize;          // sum of content sizes of collected Data
  };

  ndn::Name m_prefix;          // Aggregation prefix this app serves
  uint32_t  m_producerCount;   // Number of producers (sub-Interests) to query
  Time      m_freshness;       // Freshness period for aggregated Data
  std::map<std::string, PendingRequest> m_pending;  // map of request ID to pending info
  uint32_t m_seq;              // sequence number for generating unique request IDs
};

} // namespace ndn
} // namespace ns3

#endif // NDN_AGGREGATOR_HPP
