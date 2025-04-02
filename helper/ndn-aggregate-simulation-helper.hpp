#ifndef NDN_AGGREGATE_SIMULATION_HELPER_HPP
#define NDN_AGGREGATE_SIMULATION_HELPER_HPP

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/ndnSIM-module.h"

// Include the custom producer
#include "ns3/ndnSIM/apps/ndn-value-producer.hpp"

// Include the custom strategy
#include "ns3/ndnSIM/NFD/daemon/fw/AggregateStrategy.hpp"

// Include the utility class
#include "ns3/ndnSIM/utils/ndn-aggregate-utils.hpp"

namespace ns3 {
namespace ndn {

/**
 * @brief Helper class for aggregate sum simulation
 */
class AggregateSimulationHelper
{
public:
  /**
   * @brief Constructor
   */
  AggregateSimulationHelper();
  
  //
  // TOPOLOGY MANAGEMENT
  //
  
  /**
   * @brief Set number of producer-consumer nodes
   */
  void SetNodeCount(int count);
  
  /**
   * @brief Create the topology with all nodes
   * @return The created nodes
   */
  NodeContainer CreateTopology();
  
  /**
   * @brief Print ASCII diagram of the topology
   */
  void PrintTopologyDiagram() const;
  
  /**
   * @brief Get producer node IDs
   */
  const std::vector<int>& GetProducerIds() const;
  
  //
  // APPLICATION MANAGEMENT
  //
  
  /**
   * @brief Install producer applications on nodes
   */
  void InstallProducers(const NodeContainer& nodes);
  
  /**
   * @brief Configure routing for aggregation
   */
  void ConfigureRouting(const NodeContainer& nodes);
  
  /**
   * @brief Install consumer applications for aggregation
   */
  void InstallConsumers(const NodeContainer& nodes);
  
  //
  // MONITORING AND TRACING
  //
  
  /**
   * @brief Set up data monitoring for nodes
   */
  void SetupDataMonitoring();
  
  /**
   * @brief Enable packet tracing
   */
  void EnablePacketTracing();

  /**
   * @brief Install AggregateStrategy on all nodes for the aggregate prefix
   */
  void InstallStrategy();

  /**
   * @brief Verify strategy installation on sample node
   * @param nodes The container with all nodes
   */
  void VerifyStrategyInstallation(const NodeContainer& nodes);
  
  /**
   * @brief Verify FIB entries on each node
   * @param nodes The container with all nodes
   */
  void VerifyFibEntries(const NodeContainer& nodes);
  
  /**
   * @brief Install application tracers
   * @param tracePath Directory to store trace files
   */
  void InstallTracers(const std::string& tracePath);

private:
  // Topology variables
  int m_nodeCount;
  std::vector<int> m_producerIds;
  std::vector<int> m_rackAggregatorIds;
  std::vector<int> m_coreAggregatorIds;
  NodeContainer m_nodes;
  
  // Monitoring helpers
  bool ShouldMonitorNode(ns3::ndn::AggregateUtils::NodeRole role);
  void ProcessReceivedData(const ::ndn::Data& data, const std::string& roleString, 
                          uint32_t faceId, Ptr<ns3::ndn::L3Protocol> ndnProtocol);
  void SetupFaceMonitoring(const nfd::Face& face, Ptr<ns3::ndn::L3Protocol> ndnProtocol,
                          uint32_t nodeId, const std::string& roleString);
  void SetupNodeMonitoring(Ptr<Node> node, uint32_t nodeIndex, const std::string& roleString);
  
  // Trace callback functions
  static void MacTxTrace(std::string context, Ptr<const Packet> packet);
  static void MacRxTrace(std::string context, Ptr<const Packet> packet);
};

} // namespace ndn
} // namespace ns3

#endif // NDN_AGGREGATE_SIMULATION_HELPER_HPP