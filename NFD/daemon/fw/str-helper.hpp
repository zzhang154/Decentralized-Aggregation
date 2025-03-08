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
 * - PCON : str-helper
 */

#include "mt-forwarding-info.hpp"

namespace nfd {
namespace fw {

using ndn::Name; 

class StrHelper
{
public:
  static const uint DOWN_FACE_METRIC = 7;

public:

  static void
  reduceFwPerc(shared_ptr<MtForwardingInfo> forwInfo,
               const FaceId reducedFaceId,
               const double change)
  {

    if (forwInfo->getFaceCount() == 1) {
      std::cout << "Trying to update fw perc of single face!\n";
      return;
    }
  }

  /** \brief determines whether a NextHop is eligible
   *  \param currentDownstream incoming FaceId of current Interest
   *  \param wantUnused if true, NextHop must not have unexpired OutRecord
   *  \param now time::steady_clock::now(), ignored if !wantUnused
   */
  static bool
  predicate_NextHop_eligible(const shared_ptr<pit::Entry>& pitEntry,
      const fib::NextHop& nexthop,
      FaceId currentDownstream, 
      bool wantUnused = false,
      ndn::time::steady_clock::TimePoint now = ndn::time::steady_clock::TimePoint::min())
  // TODO Implement congestion action.
  {
    // shared_ptr<Face> upstream = nexthop.getFace();

    // // upstream is current downstream
    // if (upstream->getId() == currentDownstream)
    //   return false;
    // // forwarding would violate scope
    // if (pitEntry->violatesScope(*upstream))
    //   return false;
    // if (upstream->getMetric() == DOWN_FACE_METRIC) {
    //   return false;
    // }

    // if (wantUnused) {
    //   // NextHop must not have unexpired OutRecord
    //   pit::OutRecordCollection::const_iterator outRecord = pitEntry->getOutRecord(*upstream);
    //   if (outRecord != pitEntry->getOutRecords().end() && outRecord->getExpiry() > now) {
    //     return false;
    //   }
    // }

    // return true;
  }


  // static shared_ptr<MtForwardingInfo>
  static MtForwardingInfo*
  getPrefixMeasurements(const fib::Entry& fibEntry, MeasurementsAccessor& measurements)
  {
    /* 改函数用于查找指定FIB中的内容是否构建了measurement结构
    改动：
    - 新版本measurements.get() 返回的内容变成了指针，修改此处的适配
    - measurements::Entry.getStrategyInfo() 返回内容也变成了指针，返回类型做个适配
    */
    measurements::Entry* me = measurements.get(fibEntry);
    // shared_ptr<measurements::Entry> me = measurements.get(fibEntry);

    if (me == nullptr) {
      std::cout << "Didn't find measurement entry for name: " << fibEntry.getPrefix() << "\n";
      return nullptr;
    }
    return me->getStrategyInfo<MtForwardingInfo>();
  }

  // static shared_ptr<MtForwardingInfo>
  static MtForwardingInfo*
  addPrefixMeasurements(const fib::Entry& fibEntry, MeasurementsAccessor& measurements)
  {
    // shared_ptr<measurements::Entry> me = measurements.get(fibEntry);
    measurements::Entry* me = measurements.get(fibEntry);

    // 原来的函数调整没了
    std::pair<MtForwardingInfo*, bool> res = me->insertStrategyInfo<MtForwardingInfo>();
    return res.first;
    // return me->getOrCreateStrategyInfo<MtForwardingInfo>();
    
  }

  static std::string
  getTime()
  {
    std::ostringstream oss;
    oss << ndn::time::steady_clock::now().time_since_epoch().count() / 1000000 << " ms";
    return oss.str();
  }

  static bool
  getEnvVariable(std::string name, bool defaultValue)
  {
    std::string tmp = StrHelper::getEnvVar(name);
    if (!tmp.empty()) {
      return (tmp == "TRUE" || tmp == "true");
    }
    else {
      return defaultValue;
    }
  }

  static int
  getEnvVariable(std::string name, int defaultValue)
  {
    std::string tmp = StrHelper::getEnvVar(name);
    if (!tmp.empty()) {
      return std::stoi(tmp);
    }
    else {
      return defaultValue;
    }
  }

  static double
  getDoubleEnvVariable(std::string name, double defaultValue)
  {
    std::string tmp = StrHelper::getEnvVar(name);
    if (!tmp.empty()) {
      return std::stod(tmp);
    }
    else {
      return defaultValue;
    }
  }

private:

  static std::string
  getEnvVar(std::string const& key)
  {
    char const* val = getenv(key.c_str());
    return val == NULL ? std::string() : std::string(val);
  }

};

} //fw
} //nfd