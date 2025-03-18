#ifndef NDN_VALUE_PRODUCER_H
#define NDN_VALUE_PRODUCER_H

#include "ns3/ndnSIM/apps/ndn-app.hpp"
#include "ns3/nstime.h"

namespace ns3 {
namespace ndn {

/**
 * @brief NDN application that combines producer and consumer functionality
 *
 * This application has dual behavior:
 * 1. As producer: responds to Interests matching its node ID with data containing its node ID value
 * 2. As consumer: can send a single Interest for a specified prefix (if configured)
 */
class ValueProducer : public App {
public:
  /**
   * @brief Default constructor
   */
  ValueProducer();

  /**
   * @brief Get TypeId of the class
   */
  static TypeId GetTypeId();

  /**
   * @brief Set the node ID explicitly (otherwise uses NS3 node ID)
   */
  void SetNodeId(int nodeId) { m_nodeId = nodeId; }

protected:
  // Overridden from Application base class
  virtual void StartApplication() override;
  virtual void StopApplication() override;
  
  /**
   * @brief Callback for Interest reception
   * 
   * Creates and returns Data containing this node's ID value
   * if the Interest name matches this node's prefix
   */
  virtual void OnInterest(std::shared_ptr<const ::ndn::Interest> interest) override;

  /**
   * @brief Called when Data is received
   * 
   * Processes received data, ignoring data produced by this node itself
   */
  virtual void OnData(std::shared_ptr<const ::ndn::Data> data) override;

  /**
   * @brief Helper method to debug PIT state
   */
  void DebugPitState(const ::ndn::Name& interestName);

  void DebugFibEntries(const std::string& message);

  void DebugFaceStats();

  /**
   * @brief Sends a single Interest (for consumer functionality)
   */
  void SendOneInterest();

  // Add to protected or private section:
  void ForwardDataToNetwork(std::shared_ptr<const ::ndn::Data> data);

private:
  int m_nodeId;               ///< Node ID to return as value
  ::ndn::Name m_prefix;       ///< Interest prefix to use for consumer role
  ns3::Time m_interestLifetime; ///< Interest lifetime as ns3::Time (converted to ndn::time when sending)
  TracedCallback<
  std::shared_ptr<const ::ndn::Interest>,
  ValueProducer*,  // match the 'this' pointer
  Face*            // match the raw pointer for the face
> m_transmittedInterests;
};

} // namespace ndn
} // namespace ns3

#endif // NDN_VALUE_PRODUCER_H