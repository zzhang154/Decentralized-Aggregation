/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/**
 * Copyright (c) 2011-2015  Regents of the University of California.
 *
 * This file is part of ndnSIM. See AUTHORS for complete list of ndnSIM authors and
 * contributors.
 *
 * ndnSIM is free software: you can redistribute it and/or modify it under the terms
 * of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * ndnSIM is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 * PURPOSE.  See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * ndnSIM, e.g., in COPYING.md file.  If not, see <http://www.gnu.org/licenses/>.

 * Author : z2h
 * Last modified : 23-04-03
 * brief : 测试一些用户发送场景
 **/

#ifndef NDN_CONSUMER_TEST_H
#define NDN_CONSUMER_TEST_H

#include "ns3/ndnSIM/model/ndn-common.hpp"

#include "ndn-consumer.hpp"

namespace ns3 {
namespace ndn {

/*****************************************
 **       ConsumerTest  测试场景用
 *****************************************/
/**
 * @ingroup ndn-apps
 * @brief ndn application for sending out interest packet at a z-mi rate
 */
class ConsumerTest : public Consumer {
public:
  static TypeId
  GetTypeId();

  ConsumerTest();

  virtual ~ConsumerTest();
  
  virtual void
  StartApplication();

  virtual void
  OnData(shared_ptr<const Data> contentObject);

  virtual void
  OnNack(shared_ptr<const lp::Nack> nack);

  virtual void
  WillSendOutInterest(uint32_t sequenceNumber);

  void
  SendPacket();

protected:
  virtual void
  ScheduleNextPacket();

private:
  void 
  SyncSeqNumber(shared_ptr<const Data> contentObject);      // 同步发送序号

  void
  UpdateSyncTime();

  uint32_t
  GetInterestSeq();

  uint32_t
  GetSyncSeq();

private:
  Time GetCurrentTime()                      // 获得当前时间
  {
    return MilliSeconds(time::toUnixTimestamp(time::system_clock::now()).count());   
  }

private:
  double m_sendRate;

  uint32_t m_syncSeq;

  Time m_syncInterval;        // 同步时间间隔
  Time m_nextSyncTime;

  bool m_syncFlag;            // 同步标记

};


} // namespace ndn
} // namespace ns3

#endif