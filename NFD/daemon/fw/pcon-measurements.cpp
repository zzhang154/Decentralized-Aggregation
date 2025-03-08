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
 * Last Modified : 2022-11-02
 * 
 */


#include "pcon-measurements.hpp"
#include "common/global.hpp"
#include "algorithm.hpp"

#include "common/logger.hpp"


namespace nfd {
namespace fw {
namespace pcon {

NFD_LOG_INIT(PconMeasurements);

NamespaceInfo::NamespaceInfo(std::string name)
  : m_ownPrefix(name)
{
}

/* 通过fibEntry初始化
 *  会自动根据下一跳接口的数量来创建转发概率表
 */
NamespaceInfo::NamespaceInfo(const fib::Entry& fibEntry)
{
  m_ownPrefix = fibEntry.getPrefix().toUri();
  initializeForwMap(fibEntry);
}

/* 初始化转发概率 */
void
NamespaceInfo::initializeForwMap(const fib::Entry& fibEntry)
{
  const nfd::fib::NextHopList& nexthops = fibEntry.getNextHops();
  
  if (!hasLocalNextHop(fibEntry)) {                 // 下一跳没有本地接口，均分转发概率
    double perc = 1.0 / (double) nexthops.size();
    for (auto n : nexthops) {
      NFD_LOG_DEBUG("InitializeForw: Face: " << n.getFace().getId() << " perc: " << perc);
      this->setforwPerc(n.getFace().getId(), perc);
    }
  } 
  else {                                            // 下一跳有本地接口 将lowestId的转发概率置为1
    FaceId lowestId = std::numeric_limits<int>::max();
    for (auto n : nexthops) {
      if (n.getFace().getScope() == ndn::nfd::FACE_SCOPE_LOCAL) {
        lowestId = (n.getFace().getId() < lowestId) ? n.getFace().getId() : lowestId;
      }
    }
    this->setforwPerc(lowestId, 1.0);
  }
}

/* 获取接口的转发概率 */
double
NamespaceInfo::getforwPerc(FaceId face)
{
  if (m_forwPercMap.find(face) == m_forwPercMap.end()) {
    NFD_LOG_DEBUG("Couldn't find face " << face);
  }
  assert(m_forwPercMap.find(face) != m_forwPercMap.end());
  double forwPerc = m_forwPercMap.at(face);
  assert(forwPerc >= 0 && forwPerc <= 1);
  return forwPerc;
}

/* 接口数量 */
int
NamespaceInfo::getFaceCount()
{
  std::vector<FaceId> faceIdList;
  for (auto faceInfo : m_forwPercMap) {
      faceIdList.push_back(faceInfo.first);
  }
  return faceIdList.size();
}

void
NamespaceInfo::showForwMap()
{
  NFD_LOG_DEBUG("Forwarding Map: ");
  for (auto &[key, val]: m_forwPercMap) {
    NFD_LOG_DEBUG(" - FaceId: " << key << " FwPerc: " << val);
  }
}


////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

constexpr time::microseconds PconMeasurements::MEASUREMENTS_LIFETIME;

NamespaceInfo*
PconMeasurements::getNamespaceInfo(const fib::Entry& fibEntry)
{
  // 没有measurement entry就直接返回空，StrategyInfo都是建立在fibEntry下的
  // auto* me = m_measurements.findLongestPrefixMatch(prefix);
  auto* me = m_measurements.get(fibEntry);
  if (me == nullptr) {
    return nullptr;
  }

  // Set or update entry lifetime
  extendLifetime(*me);

  // insertStrategyInfo内部会判断该内容是否已经存在，从而选择建立新内容
  NamespaceInfo* info = me->insertStrategyInfo<NamespaceInfo>(fibEntry).first;

  BOOST_ASSERT(info != nullptr);
  return info;
}

// NamespaceInfo&
// PconMeasurements:: getOrCreateNamespaceInfo(const fib::Entry& fibEntry, const Name& prefix)
// {
//   auto* me = m_measurements.get(fibEntry);

//   // If the FIB entry is not under the strategy's namespace, find a part of the prefix
//   // that falls under the strategy's namespace
//   for (size_t prefixLen = fibEntry.getPrefix().size() + 1;
//        me == nullptr && prefixLen <= prefix.size();
//        ++prefixLen) {
//     me = m_measurements.get(prefix.getPrefix(prefixLen));
//   }

//   // Either the FIB entry or the Interest's name must be under this strategy's namespace
//   BOOST_ASSERT(me != nullptr);

//   // Set or update entry lifetime
//   extendLifetime(*me);

//   // 创建并插入measurement条目
//   NamespaceInfo* info = me->insertStrategyInfo<NamespaceInfo>().first;
//   BOOST_ASSERT(info != nullptr);
//   return *info;
// }

void
PconMeasurements::extendLifetime(measurements::Entry& me)
{
  m_measurements.extendLifetime(me, MEASUREMENTS_LIFETIME);
}



} // pcon
} // fw
} // nfd
