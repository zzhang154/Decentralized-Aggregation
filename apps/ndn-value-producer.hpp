#ifndef NDN_VALUE_PRODUCER_H
#define NDN_VALUE_PRODUCER_H

#include "ns3/ndnSIM/apps/ndn-app.hpp"

namespace ns3 {
namespace ndn {

// NDN Producer Application - produces data with values based on node ID
class ValueProducer : public App {
public:
  // Add explicit default constructor
  ValueProducer() : m_nodeId(0) {}

  static TypeId GetTypeId();
  void SetNodeId(int nodeId) { m_nodeId = nodeId; }

protected:
  // Overridden from Application base class
  virtual void StartApplication();
  
  // Callback for Interest reception
  virtual void OnInterest(std::shared_ptr<const ::ndn::Interest> interest);

private:
  int m_nodeId; // Node ID to return as value
};

} // namespace ndn
} // namespace ns3

#endif // NDN_VALUE_PRODUCER_H