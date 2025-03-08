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

#ifndef NFD_DAEMON_FW_PCON_MEASUREMENTS_HPP
#define NFD_DAEMON_FW_PCON_MEASUREMENTS_HPP

#include "face/face.hpp"
#include "strategy-info.hpp"
#include "table/measurements-accessor.hpp"

namespace nfd {
namespace fw {
namespace pcon {

class NamespaceInfo : public StrategyInfo
{
public:
  static constexpr int
  getTypeId()
  {
    return 1013;
  }

  explicit
  NamespaceInfo(std::string name);

  explicit
  NamespaceInfo(const fib::Entry& fibEntry);

  double
  getforwPerc(FaceId face);

  int
  getFaceCount();

  /* 初始化转发概率表 */ 
  void
  initializeForwMap(const fib::Entry& fibEntry);

  void
  showForwMap();        // 显示转发表

public:
  void
  setforwPerc(FaceId faceId, double perc)
  {
    m_forwPercMap[faceId] = perc;
  }

  void
  increaseforwPerc(FaceId faceId, double changeRate)
  {
    m_forwPercMap[faceId] += changeRate;
  }

  const std::map<FaceId, double>
  getForwPercMap() const
  {
    return m_forwPercMap;
  }

  void
  setPrefix(std::string prefix)
  {
    m_ownPrefix = prefix;
  }

  std::string
  getPrefix() const
  {
    return m_ownPrefix;
  }

private:

  std::string m_ownPrefix;
  std::map<FaceId, double> m_forwPercMap;
  std::unordered_set<FaceId> m_disabledFaces;

};

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

class PconMeasurements : noncopyable
{
public:
  explicit
  PconMeasurements(MeasurementsAccessor& measurements)
        : m_measurements(measurements) 
  {
  }

  // 
  NamespaceInfo*
  getNamespaceInfo(const fib::Entry& fibEntry);

  NamespaceInfo&
  getOrCreateNamespaceInfo(const fib::Entry& fibEntry, const Name& prefix);

private:
  void
  extendLifetime(measurements::Entry& me);

public:
  static constexpr time::microseconds MEASUREMENTS_LIFETIME = 5_min;

private:
  MeasurementsAccessor& m_measurements;         // 这个是measurements_table对象
};


} // pcon
} // fw
} // nfd


#endif //NFD_DAEMON_FW_PCON_MEASUREMENTS_HPP