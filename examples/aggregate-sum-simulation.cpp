// aggregate-sum-simulation.cpp - Clean modular implementation

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

// Include our helper
#include "ns3/ndnSIM/helper/ndn-aggregate-simulation-helper.hpp"

using namespace ns3;

// Declare the NodeCount global value (must be at global scope)
static ns3::GlobalValue g_nodeCount("NodeCount",
  "Number of consumer-producer nodes",
  ns3::UintegerValue(5), // Default value
  ns3::MakeUintegerChecker<uint32_t>(1, 100)); // Min and max values

/**
 * Initialize simulation: parse command line args and set up logging
 */
void 
initializeSimulation(int argc, char* argv[], int& nodeCount) 
{
  std::cout << "=== INITIALIZING SIMULATION ===" << std::endl;
  
  // Parse command line arguments
  CommandLine cmd;
  cmd.AddValue("nodeCount", "Number of consumer-producer in the network", nodeCount);
  cmd.Parse(argc, argv);

  // Bind to global value
  ns3::GlobalValue::Bind("NodeCount", ns3::UintegerValue(nodeCount));
  
  // Set up logging
  LogComponentEnable("ndn.AggregateStrategy", LOG_LEVEL_INFO);
  LogComponentEnable("ndn.ValueProducer", LOG_LEVEL_INFO);
  
  std::cout << "Node count: " << nodeCount << std::endl;
}

/**
 * Configure time markers for the simulation
 */
void 
configureTimeMarkers() 
{
  // Add time markers
  for (int t = 0; t <= 5; t++) {
    Simulator::Schedule(Seconds(t), [t]() {
      std::cout << "Time: " << t << "s" << std::endl << std::flush;
    });
  }
}

/**
 * Main function
 */
int 
main(int argc, char* argv[]) 
{
  // Default node count
  int nodeCount = 5;
  
  // Initialize simulation
  initializeSimulation(argc, argv, nodeCount);

  // Create a single helper for our entire simulation
  // Use the fully qualified namespace to avoid ambiguity
  ns3::ndn::AggregateSimulationHelper helper;
  helper.SetNodeCount(nodeCount);
  
  // Create topology
  NodeContainer nodes = helper.CreateTopology();
  helper.PrintTopologyDiagram();
  
  // Enable packet tracing
  helper.EnablePacketTracing();
  
  // Install NDN stack
  ns3::ndn::StackHelper ndnHelper;
  ndnHelper.setCsSize(0);  // Disable content store
  ndnHelper.InstallAll();
  
  // Install strategy
  helper.InstallStrategy();
  helper.VerifyStrategyInstallation(nodes);
  
  // Setup monitoring 
  helper.SetupDataMonitoring();
  
  // Install applications and configure routing
  helper.InstallProducers(nodes);
  helper.ConfigureRouting(nodes);
  helper.InstallConsumers(nodes);
  
  // Verify FIB entries
  helper.VerifyFibEntries(nodes);

  // Install tracers
  helper.InstallTracers("results/");
  
  // Add time markers
  configureTimeMarkers();
  
  // Run simulation
  std::cout << "\n=== RUNNING SIMULATION ===" << std::endl;
  Simulator::Stop(Seconds(5.0));
  Simulator::Run();
  Simulator::Destroy();
  
  std::cout << "\n=== SIMULATION COMPLETE ===" << std::endl;
  return 0;
}