#ifndef NDN_VALUE_PRODUCER_H
#define NDN_VALUE_PRODUCER_H

#include "ns3/ndnSIM/apps/ndn-app.hpp"
#include "ns3/nstime.h"
#include "ns3/ndnSIM/utils/ndn-aggregate-utils.hpp"

namespace ns3 {
namespace ndn {

/**
 * @brief NDN application that combines producer and consumer functionality
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

  void PrintFibState(const std::string& message) {
    DebugFibEntries(message);
  }

protected:
  // Overridden from Application base class
  virtual void StartApplication() override;
  // virtual void StopApplication() override;
  
  /**
   * @brief Callback for Interest reception
   */
  virtual void OnInterest(std::shared_ptr<const ::ndn::Interest> interest) override;

  /**
   * @brief Called when Data is received
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

  /**
   * @brief Forward data to network
   */
  void ForwardDataToNetwork(std::shared_ptr<const ::ndn::Data> data);

  void ForwardToStrategy(std::shared_ptr<const ::ndn::Interest> interest);

private:
  int m_nodeId;               ///< Node ID to return as value
  ::ndn::Name m_prefix;       ///< Interest prefix to use for consumer role
  ns3::Time m_interestLifetime; ///< Interest lifetime as ns3::Time
  uint32_t m_seqNo; // Per-instance sequence number counter
  
  // Add these missing member variables:
  int m_payloadSize;          ///< Size of payload in Data packet
  ns3::Time m_freshness;      ///< Data packet freshness period
  
  TracedCallback<
    std::shared_ptr<const ::ndn::Interest>,
    ValueProducer*,  // match the 'this' pointer
    Face*            // match the raw pointer for the face
  > m_transmittedInterests;
};

} // namespace ndn
} // namespace ns3

#endif // NDN_VALUE_PRODUCER_H