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
 * Last modified : 22-11-14
 * brief : DPCCP用户端发送逻辑
 **/

#include "ndn-consumer-test.hpp"

#include "ns3/ptr.h"
#include "ns3/log.h"
#include "ns3/simulator.h"
#include "ns3/packet.h"
#include "ns3/callback.h"
#include "ns3/string.h"
#include "ns3/boolean.h"
#include "ns3/uinteger.h"
#include "ns3/integer.h"
#include "ns3/double.h"

#include <limits.h>
#include <ndn-cxx/util/random.hpp>
#include <ndn-cxx/lp/tags.hpp>
#include <ndn-cxx/name-component.hpp>

NS_LOG_COMPONENT_DEFINE("ndn.ConsumerTest");

namespace ns3 {
namespace ndn {

NS_OBJECT_ENSURE_REGISTERED(ConsumerTest);

TypeId
ConsumerTest::GetTypeId(void)
{
  static TypeId tid =
    TypeId("ns3::ndn::ConsumerTest")
      .SetGroupName("Ndn")
      .SetParent<Consumer>()
      .AddConstructor<ConsumerTest>()
      // .AddAttribute("Frequency", "Frequency of interest packets", StringValue("1.0"),         // 客户端可以通过SetAttribute指定属性
      //               MakeDoubleAccessor(&ConsumerTest::m_frequency), MakeDoubleChecker<double>())

    ;

  return tid;
}

ConsumerTest::ConsumerTest()
  : m_sendRate(20)
{
  NS_LOG_FUNCTION_NOARGS();
  m_seqMax = std::numeric_limits<uint32_t>::max();    // 一定要指定这句，不然不会有发包行为
}

ConsumerTest::~ConsumerTest()
{
}

/*
 * 暂时没啥修改的地方
 */
void
ConsumerTest::WillSendOutInterest(uint32_t sequenceNumber)
{
  Consumer::WillSendOutInterest(sequenceNumber);
}

void
ConsumerTest::StartApplication() // Called at time specified by Start
{
  NS_LOG_FUNCTION_NOARGS();
  App::StartApplication();

  m_syncSeq = 0;
  m_syncInterval = Seconds(3.0);
  this->UpdateSyncTime();

  ScheduleNextPacket();
}

void
ConsumerTest::UpdateSyncTime()
{
  Time cur_time = this->GetCurrentTime();
  m_nextSyncTime = cur_time + m_syncInterval;
  NS_LOG_INFO("NextSyncTime: " << m_nextSyncTime.GetSeconds());
}

/*
 * Nack包响应
 */
void
ConsumerTest::OnNack(shared_ptr<const lp::Nack> nack)
{
  App::OnNack(nack);
  NS_LOG_INFO("NACK received for: " << nack->getInterest().getName()
              << ", reason: " << nack->getReason());
}


/*
 * 模拟一下随机时间间隔发包
 */
void
ConsumerTest::ScheduleNextPacket()
{
  // double next_send_time = 1.0 / m_sendRate;
  double next_send_time = (m_syncFlag) ? 0.01 : 1.0 / m_sendRate;

  if (m_syncFlag) {
    m_syncFlag = false;
  } else {
    std::uniform_real_distribution<double> distribution(-1 * next_send_time, next_send_time);
    std::random_device rd;
    std::mt19937 r_eng(rd());

    auto val = distribution(r_eng);
    next_send_time += val;
  }

  NS_LOG_DEBUG("Next send time : " << next_send_time);

  if (!m_sendEvent.IsRunning()) {
    m_sendEvent = Simulator::Schedule(Seconds(next_send_time), &ConsumerTest::SendPacket, this);             // 默认发送方式
  }
}


uint32_t
ConsumerTest::GetSyncSeq()
{
  uint32_t seq = std::numeric_limits<uint32_t>::max(); // invalid

  while (m_retxSeqs.size()) {
    seq = *m_retxSeqs.begin();
    m_retxSeqs.erase(m_retxSeqs.begin());
    break;
  }

  if (seq == std::numeric_limits<uint32_t>::max()) {
    if (m_seqMax != std::numeric_limits<uint32_t>::max()) {
      if (m_syncSeq >= m_seqMax) {
        return m_syncSeq; // we are totally done
      }
    }
    seq = m_syncSeq++;
  }
  return seq;
}

/*
 * 获取发送兴趣包序列号 （从原本的发送函数中提取）
 */
uint32_t
ConsumerTest::GetInterestSeq()
{
  uint32_t seq = std::numeric_limits<uint32_t>::max(); // invalid

  while (m_retxSeqs.size()) {
    seq = *m_retxSeqs.begin();
    m_retxSeqs.erase(m_retxSeqs.begin());
    break;
  }

  if (seq == std::numeric_limits<uint32_t>::max()) {
    if (m_seqMax != std::numeric_limits<uint32_t>::max()) {
      if (m_seq >= m_seqMax) {
        return m_seq; // we are totally done
      }
    }
    seq = m_seq++;
  }
  return seq;
}

/*
 * 发包函数
 *  - 添加一个同步功能
 */
void
ConsumerTest::SendPacket()
{
  if (!m_active)
    return;

  NS_LOG_FUNCTION_NOARGS();

  shared_ptr<Name> nameWithSequence = make_shared<Name>(m_interestName);

  uint32_t seq = m_seqMax;

  Time cur_time = this->GetCurrentTime();
  if (cur_time < m_nextSyncTime) {                        // 正常数据包
    seq = this->GetInterestSeq();
  } else {                                                // sync 同步兴趣包
    this->UpdateSyncTime();

    m_syncFlag = true;
    seq = this->GetSyncSeq();
    std::string sync_prefix = "sync";
    nameWithSequence->append(ndn::name::Component(sync_prefix));              // 添加同步标记
  } 
  
  if (seq >= m_seqMax) {return;}

  nameWithSequence->appendSequenceNumber(seq);
  //

  // shared_ptr<Interest> interest = make_shared<Interest> ();
  shared_ptr<Interest> interest = make_shared<Interest>();
  interest->setNonce(m_rand->GetValue(0, std::numeric_limits<uint32_t>::max()));
  interest->setName(*nameWithSequence);
  interest->setCanBePrefix(false);
  time::milliseconds interestLifeTime(m_interestLifeTime.GetMilliSeconds());
  interest->setInterestLifetime(interestLifeTime);

  // NS_LOG_INFO ("Requesting Interest: \n" << *interest);
  NS_LOG_INFO("> Interest for " << seq);

  WillSendOutInterest(seq);

  m_transmittedInterests(interest, this, m_face);
  m_appLink->onReceiveInterest(*interest);

  ScheduleNextPacket();
}


/*
 * 同步序列号
 */
void 
ConsumerTest::SyncSeqNumber(shared_ptr<const Data> contentObject)
{
  auto tag_ptr = contentObject->getTag<lp::CongestionMarkTag>();

  if (tag_ptr == nullptr) {return; }

  auto seq_number = tag_ptr->get();

  NS_LOG_INFO("[Sync Seq]: Consumer Seq: " << m_seq << " Server Seq: " << seq_number);

  m_seq = (m_seq > seq_number) ? m_seq : seq_number;
}


/*
 * 数据包响应
 *  - 解析路径ID
 *  - 判断是否为新路径
 *  - 交由指定路径的状态机处理
 *  - 
 */
void
ConsumerTest::OnData(shared_ptr<const Data> contentObject)
{
  // SequenceNumber32 cur_seq = SequenceNumber32(contentObject->getName().at(-1).toSequenceNumber());
  uint64_t cur_seq = contentObject->getName().at(-1).toSequenceNumber();
  
  // Time cur_rtt = m_rtt->GetCurrentEstimate();
  Time cur_rtt = m_rtt->AckSeq(SequenceNumber32(cur_seq));              // 准确的Rtt

  NS_LOG_DEBUG("------------------------------------------------------------");
  NS_LOG_DEBUG("receive data:" << contentObject->getName().toUri() << " Rtt: " << cur_rtt.GetMilliSeconds() << "ms");

  this->SyncSeqNumber(contentObject);


  // NS_LOG_DEBUG("Rtt: " << cur_rtt.GetMilliSeconds() << "ms, Rtt2: " << tmp_rtt.GetMilliSeconds() << "ms");

  // ScheduleNextPacket();
  Consumer::OnData(contentObject);          // 会重置RTT的调控信息
}






} // namespace ndn
} // namespace ns3
