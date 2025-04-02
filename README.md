# Decentralized Data Aggregation in Named Data Networking

## Overview

This project implements a decentralized data aggregation framework for Named Data Networking (ndnSIM 2.9). The system enables distributed nodes to compute aggregate functions collaboratively over their values using NDN's data-centric approach.

## Project Structure

### Components and Their Locations

1. **Utility Library** (`/src/ndnSIM/utils/`)
   - `ndn-aggregate-utils.hpp` / `ndn-aggregate-utils.cpp`
   - Provides helper functions for name manipulation, data extraction, and interest processing

2. **Custom Forwarding Strategy** (`/src/ndnSIM/NFD/daemon/fw/`)
   - `AggregateStrategy.hpp` / `AggregateStrategy.cpp`
   - Implements an NDN forwarding strategy that handles in-network aggregation

3. **Producer-Consumer Application** (`/src/ndnSIM/apps/`)
   - `ndn-value-producer.hpp` / `ndn-value-producer.cpp`
   - Hybrid application that both produces data and requests data from other nodes

4. **Simulation Helper** (`/src/ndnSIM/helper/`)
   - `ndn-aggregate-simulation-helper.hpp` / `ndn-aggregate-simulation-helper.cpp`
   - Creates topology, installs applications, configures routing, and sets up monitoring

5. **Main Simulation** (`/src/ndnSIM/examples/`)
   - `aggregate-sum-simulation.cpp`
   - Entry point that orchestrates all components

## Component Relationships

- **Main Simulation** (`aggregate-sum-simulation.cpp`)
  - Includes all other components
  - Creates a helper instance that manages the simulation
  - Configures global parameters like node count
  - Runs the simulation

- **Simulation Helper** (`ndn-aggregate-simulation-helper`)
  - Creates the network topology (producers, rack aggregators, core aggregators)
  - Installs applications on nodes
  - Configures routing tables
  - Sets up monitoring and tracing

- **Value Producer** (`ndn-value-producer`)
  - Acts as both producer and consumer
  - Generates its own data value (based on node ID)
  - Sends interests to request data from other nodes
  - Forwards interests to the NDN forwarding plane

- **Aggregate Strategy** (`AggregateStrategy`)
  - Custom NDN forwarding strategy that handles aggregate interests
  - Splits interests based on routing information
  - Combines partial results for complete aggregations
  - Manages parent-child relationships between interests

- **Aggregate Utils** (`ndn-aggregate-utils`)
  - Used by all other components
  - Provides name parsing and manipulation
  - Extracts values from data packets
  - Helps with interest and data creation

## How It Works

1. Each producer node has a unique numeric value (its node ID)
2. Nodes send interests requesting aggregated values from multiple other nodes
3. The AggregateStrategy splits these interests into sub-interests as needed
4. Sub-interests are forwarded based on the routing table
5. When data returns, partial results are combined
6. Once all needed values are collected, the final aggregated result is returned

The framework abstracts the complexity of interest splitting, data aggregation, and routing from the application layer, providing a clean interface for distributed computation.

## Implementation Details

The implementation uses a hierarchical topology with producer nodes, rack aggregators, and core aggregators to model a realistic network structure. The `ValueProducer` application acts as both a producer of local values and a consumer requesting aggregated values, while the `AggregateStrategy` handles the forwarding and aggregation logic in the network layer.

## Demonstration: In-Network Data Aggregation

The following example demonstrates how the framework performs decentralized data aggregation in a hierarchical network topology.

### Sample Topology

Core Layer:         [C1]
                   /  |  \
Rack Aggregators: [R1][R2][R3]
                   |   |   |
Producers:        [P1][P2][P3]

- Producer nodes P1, P2, P3 each hold a numeric value (their node ID)
- Rack aggregators R1, R2, R3 connect producers to the core
- Core aggregator C1 connects all racks together

### Aggregation Process Walkthrough

1. **Interest Generation**: Each producer node initiates an interest for data aggregation
   - P1 sends Interest: `/aggregate/%02/%03/seq=0` (requesting values from nodes 2 and 3)
   - P2 sends Interest: `/aggregate/%01/%03/seq=0` (requesting values from nodes 1 and 3)
   - P3 sends Interest: `/aggregate/%01/%02/seq=0` (requesting values from nodes 1 and 2)

2. **Interest Forwarding**: Interests are forwarded up through the network
   - Producer nodes forward their interests to their rack aggregators
   - Rack aggregators forward to the core aggregator

3. **Interest Splitting**: When the core aggregator (C1) receives an interest requesting multiple values:
   - It consults its FIB to determine which faces can reach which producers
   - For example, when C1 receives `/aggregate/%02/%03/seq=0`:
     - It determines that node 2 is reachable via face 258 and node 3 via face 259
     - It splits the interest into sub-interests: `/aggregate/%02/seq=0` and `/aggregate/%03/seq=0`
     - It forwards each sub-interest toward the appropriate producer

4. **Data Production**: When a producer receives an interest for its own value:
   - It generates a Data packet containing its numeric value
   - For example, P2 (node 2) returns the value "2" for `/aggregate/%02/seq=0`

5. **Data Aggregation**: When data packets return to the core aggregator:
   - C1 aggregates values from sub-interests before responding to the original interest
   - It keeps track of which values belong to which original interests
   - When all needed values arrive, it computes the sum and creates an aggregate result
   - For example, for `/aggregate/%02/%03/seq=0`, it computes 2+3=5

6. **Result Delivery**: Aggregated results flow back to the requesters:
   - P1 receives a response with the sum of values from nodes 2 and 3 (5)
   - P2 receives a response with the sum of values from nodes 1 and 3 (4)
   - P3 receives a response with the sum of values from nodes 1 and 2 (3)

The entire process happens automatically through the NDN forwarding plane using the custom AggregateStrategy, without requiring the application to know network details or handle the splitting and recombination of requests.