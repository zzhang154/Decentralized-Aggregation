/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/**
 * Copyright (c) 2016 Klaus Schneider, The University of Arizona
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Author: Klaus Schneider <klaus@cs.arizona.edu>
 * 
 * Modifier : z2h
 * Last Modified : 2022-09-14
 * - Pcon in ndnsim 2
 */

#ifndef NFD_DAEMON_FW_PCON_STRATEGY_HPP
#define NFD_DAEMON_FW_PCON_STRATEGY_HPP

#include "strategy.hpp"

#include "face/generic-link-service.hpp"
#include "pcon-measurements.hpp"

#include <boost/thread/pthread/mutex.hpp>
#include <fstream>

namespace nfd {
namespace fw {
namespace pcon {

/** \brief PCON转发策略实现
 *  - 开启接口的 congestionMark 功能
 *  - 实现基于概率的多路径转发
 */

class PconStrategy : public Strategy
{
public:
  explicit
  PconStrategy(Forwarder& forwarder, const Name& name = getStrategyName());

  static const Name&
  getStrategyName();

public:           // triggers
  /* 兴趣包转发 */
  void
  afterReceiveInterest(const Interest& interest, const FaceEndpoint& ingress,
                      //  shared_ptr<fib::Entry> fibEntry,
                       const shared_ptr<pit::Entry>& pitEntry) override;

  /* 数据包转发 */
  void
  beforeSatisfyInterest(const Data& data, const FaceEndpoint& ingress,
                        const shared_ptr<pit::Entry>& pitEntry) override;

  void
  afterContentStoreHit(const Data& data, const FaceEndpoint& ingress,
                        const shared_ptr<pit::Entry>& pitEntry);

  /**
   * Sends out probed interest packets with a new nonce.
   * Currently all paths except the working path are probed whenever probingDue() returns true.
   */
  void
  probeInterests(const FaceId outFaceId, const fib::NextHopList& nexthops,
                 shared_ptr<pit::Entry> pitEntry);

private:
  /* 选择转发接口 */
  nfd::face::Face*
  chooseOutFace(const Interest& interest, const FaceEndpoint& ingress,
                const shared_ptr<pit::Entry>& pitEntry, const fib::Entry& fibEntry,
                NamespaceInfo* measurementInfo, double r_val);

private:              // 工具函数
  static void
  ReduceFwPerc(NamespaceInfo* measurementInfo,
               const FaceId reducedFaceId, const double change);   // 调整转发概率

  void
  ActiveCodelQueueMark(Face &face);                               // 激活Codel队列标记

  void
  UpdateCodelMarkInLinkService(Face &face);                       // 修改链路服务中的Option字段

private:
  std::map<FaceId, bool> m_codelQueMarkFaceMap;                   // 支持codel功能接口

public:

  static const int8_t NACK_TYPE_NONE = -1;
  static const int8_t NACK_TYPE_NO_MARK = 17;
  static const int8_t NACK_TYPE_MARK = 23;

private:

  static shared_ptr<std::ofstream> m_os;    // 用于追踪接口转发概率的变量
  static boost::mutex m_mutex;              // mutex lock

  PconMeasurements m_measurements;          // 管理该策略的度量情况

  // Forwarder& m_ownForwarder;
  time::steady_clock::TimePoint m_lastFWRatioUpdate;
  time::steady_clock::TimePoint m_lastFWWrite;

  bool INIT_SHORTEST_PATH;
  double PROBING_PERCENTAGE;
  double CHANGE_PER_MARK;

  const time::steady_clock::duration TIME_BETWEEN_FW_UPDATE;
  const time::steady_clock::duration TIME_BETWEEN_FW_WRITE;

};


} // namespace pcon
} // namespace fw
} // namespace nfd

#endif // NFD_DAEMON_FW_PCON_STRATEGY_HPP
