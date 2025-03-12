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

  // Called when Data is received - add virtual and override keywords here
  virtual void OnData(std::shared_ptr<const ::ndn::Data> data) override;

  // Helper method to debug PIT state
  void DebugPitState(const ::ndn::Name& interestName);

private:
  int m_nodeId; // Node ID to return as value
};

} // namespace ndn
} // namespace ns3

#endif // NDN_VALUE_PRODUCER_H