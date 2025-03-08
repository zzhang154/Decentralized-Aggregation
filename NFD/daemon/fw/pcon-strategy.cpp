/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * You should have received a copy of the GNU General Public License along with
 * NFD, e.g., in COPYING.md file.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Author : z2h
 * Last Modified : 2022-09-14
 * - 多路径拥塞控制策略
 */

#include "pcon-strategy.hpp"
#include "algorithm.hpp"
// #include "str-helper.hpp"

#include "common/logger.hpp"

namespace nfd {
namespace fw {
namespace pcon {

NFD_LOG_INIT(PconStrategy);           // z2h 22-07-17
NFD_REGISTER_STRATEGY(PconStrategy);

PconStrategy::PconStrategy(Forwarder& forwarder, const Name& name)
  : Strategy(forwarder)
  , m_measurements(getMeasurements())
  , m_lastFWRatioUpdate(time::steady_clock::TimePoint::min())
  , m_lastFWWrite(time::steady_clock::TimePoint::min())
  , TIME_BETWEEN_FW_UPDATE(time::milliseconds(110))
  , TIME_BETWEEN_FW_WRITE(time::milliseconds(20))
{
  // 这两行必须带，不然解析会报错
  ParsedInstanceName parsed = parseInstanceName(name);
  this->setInstanceName(makeInstanceName(name, getStrategyName()));

  // // Start all FIB entries by sending on the shortest path? 
  // // If false: start with an equal split.
  // INIT_SHORTEST_PATH = StrHelper::getEnvVariable("INIT_SHORTEST_PATH", true);

  // // How much the forwarding percentage changes for each received congestion mark
  // CHANGE_PER_MARK = StrHelper::getDoubleEnvVariable("CHANGE_PER_MARK", 0.02);

  // // How much of the traffic should be used for probing?
  // PROBING_PERCENTAGE = StrHelper::getDoubleEnvVariable("PROBING_PERCENTAGE", 0.001);
}

const Name&
PconStrategy::getStrategyName()
{
  static const auto strategyName = Name("/localhost/nfd/strategy/pcon-strategy").appendVersion(1);
  return strategyName;
}

/* 激活接口的codel标记 */
void
PconStrategy::ActiveCodelQueueMark(Face &face)
{
  auto face_id = face.getId();
  auto face_iter = m_codelQueMarkFaceMap.find(face_id);
  if (face_iter != m_codelQueMarkFaceMap.end()) {               // 有记录，说明被修改过
    return;
  }       

  this->UpdateCodelMarkInLinkService(face);                     // 激活该接口的功能
  m_codelQueMarkFaceMap[face_id] = true;                        // 标记一下
}


/* 设置标记参数 */
void
PconStrategy::UpdateCodelMarkInLinkService(Face &face)
{
  auto linkService = dynamic_cast<nfd::face::GenericLinkService*>(face.getLinkService());
  if (linkService == nullptr) {
    return;
  }
  auto options = linkService->getOptions();

  options.allowCongestionMarking = true;
  options.defaultCongestionThreshold = 20480;        // 队列长度阈值：20个包 （每个包长度1024）
  linkService->setOptions(options);

  NFD_LOG_DEBUG("[CodelMark] Activate Codel Mark in Face: " << face.getId());
}


/*****************************************
 * PCON-Fw : 数据包转发
 *  - 激活接口的队列标记功能
 *  - 根据收到的标签调整转发接口的转发概率
 *****************************************/
void
PconStrategy::beforeSatisfyInterest(const Data& data, const FaceEndpoint& ingress,
                                    const shared_ptr<pit::Entry>& pitEntry)
{
  // NFD_LOG_DEBUG(pitEntry->getInterest() << " dataFrom " << ingress << " not-fastest");

  if (!pitEntry->hasInRecords()) { return; }

  auto &in_face = ingress.face;                                 // 数据传入接口
  auto &out_face = pitEntry->in_begin()->getFace();             // 数据转出接口

  this->ActiveCodelQueueMark(out_face);                         // 激活数据接口DPCCP队列标记功能

  const fib::Entry& fibEntry = this->lookupFib(*pitEntry);
  NamespaceInfo* measurementInfo = m_measurements.getNamespaceInfo(fibEntry);           // 获取measurement,没有则会建立

  auto codel_tag = data.getTag<lp::CongestionMarkTag>();

  if (measurementInfo->getFaceCount() > 1 && in_face.getScope() != ndn::nfd::FACE_SCOPE_LOCAL)
  {
    if (codel_tag != nullptr) {
      
      double fwPerc = measurementInfo->getforwPerc(in_face.getId());
      double change_perc = 0.02 * fwPerc;

      this->ReduceFwPerc(measurementInfo, in_face.getId(), change_perc);      
    }
  }


  // if (codel_val != nullptr) {
  //   NFD_LOG_DEBUG("[CodelMark] MarkVal: " << *codel_val);
  // }

  // PitInfo* pi = pitEntry->getStrategyInfo<PitInfo>();
  // if (pi != nullptr) {
  //   pi->rtoTimer.cancel();
  // }

  // if (!pitEntry->hasInRecords()) { // already satisfied by another upstream
  //   NFD_LOG_DEBUG(pitEntry->getInterest() << " dataFrom " << ingress << " not-fastest");
  //   return;
  // }

  // auto outRecord = pitEntry->getOutRecord(ingress.face);
  // if (outRecord == pitEntry->out_end()) { // no out-record
  //   NFD_LOG_DEBUG(pitEntry->getInterest() << " dataFrom " << ingress << " no-out-record");
  //   return;
  // }

  // auto rtt = time::steady_clock::now() - outRecord->getLastRenewed();
  // NFD_LOG_DEBUG(pitEntry->getInterest() << " dataFrom " << ingress
  //               << " rtt=" << time::duration_cast<time::microseconds>(rtt).count());
  // // this->updateMeasurements(ingress.face, data, rtt);
}


void
PconStrategy::ReduceFwPerc(NamespaceInfo* measurementInfo,
               const FaceId reducedFaceId, const double change)
{
  if (measurementInfo->getFaceCount() ==1 ){
    NFD_LOG_DEBUG("Trying to update fw perc of single face!");
    return;
  }
  // 降低指定接口概率
  double changeRate = 0 - std::min(change, measurementInfo->getforwPerc(reducedFaceId));
  measurementInfo->increaseforwPerc(reducedFaceId, changeRate);

  NFD_LOG_DEBUG("Update Fw Perc: Face: " << reducedFaceId << \
                " Perc: " << measurementInfo->getforwPerc(reducedFaceId) << " ChangeRate: " << changeRate);
  // measurementInfo->showForwMap();

  // 修改其他接口概率
  double sumFWPerc = 0;
  sumFWPerc += measurementInfo->getforwPerc(reducedFaceId);
  const auto forwMap = measurementInfo->getForwPercMap();
  for (auto f : forwMap) {
    auto tempChangeRate = changeRate;
    auto &faceId = f.first;
    if (faceId == reducedFaceId) { // Do nothing. Percentage has already been added.
    }
    else {
      // Increase forwarding percentage of all other faces by and equal amount.
      tempChangeRate = std::abs(changeRate / (double) (forwMap.size() - 1));
      measurementInfo->increaseforwPerc((faceId), tempChangeRate);
      sumFWPerc += measurementInfo->getforwPerc(faceId);
    }
  }

  

  if (sumFWPerc < 0.999 || sumFWPerc > 1.001) {
    NFD_LOG_DEBUG("ERROR! Sum of fw perc out of range: " << sumFWPerc);
  }
}


/* 
 * 转发兴趣包
 *  - ContentStoreMiss 事件后触发
 */
void
PconStrategy::afterReceiveInterest(const Interest& interest, const FaceEndpoint& ingress,
                                  //  shared_ptr<fib::Entry> fibEntry,
                                   const shared_ptr<pit::Entry>& pitEntry)
{
  NS_LOG_DEBUG("------------------------------------------------------------");

  NFD_LOG_DEBUG("Processing interest : " << interest.getName());
  // const Name prefixName = pitEntry->getName();  //
  // const Name prefixName = fibEntry.getPrefix();

  // ---------- 查找nameprefix下对应的measurement
  const fib::Entry& fibEntry = this->lookupFib(*pitEntry);
  NamespaceInfo* measurementInfo = m_measurements.getNamespaceInfo(fibEntry);           // 获取measurement,没有则会建立
  if (measurementInfo == nullptr) {
    NFD_LOG_DEBUG("Didn't find measurement entry for name: " << fibEntry.getPrefix());
    return;
  }

  // ---------- 如果OutRecord中有挂起记录，则抑制来自新接口的兴趣包
  if (hasPendingOutRecords(*pitEntry)) {
    if (isComesFromNewFace(ingress.face, *pitEntry)) {
      NFD_LOG_DEBUG(" Node suppressing request from new face: " << ingress.face.getId() << ";");
      return;
    }
    // TODO：对于来自同一接口的兴趣包，设置一个新的Nonce后再进行转发（PCON原有实现逻辑）
  }

  // ---------- 转发接口决策
  double r_val = ((double) rand() / (RAND_MAX));        // 产生随机数，用于选择转发接口
  NFD_LOG_DEBUG("Random Number: " << r_val);
  measurementInfo->showForwMap();                       // 查看转发概率表

  nfd::face::Face *outFace = this->chooseOutFace(interest, ingress, pitEntry, fibEntry, measurementInfo, r_val);
  
  if (outFace == nullptr){ NFD_LOG_DEBUG("OutFace NullPtr"); }
  assert(outFace != nullptr);

  // TODO 拥塞标记部分，原本是对PIT表作拥塞标记
  NFD_LOG_DEBUG("OutFace: " << outFace->getId());
  this->sendInterest(interest, *outFace, pitEntry);

  if (r_val < 0.001) {
    this->probeInterests(outFace->getId(), fibEntry.getNextHops(), pitEntry);     // TODO 去实现一下
  }
}

/*
 * 选择转出接口
 */
nfd::face::Face*
PconStrategy::chooseOutFace(const Interest& interest, const FaceEndpoint& ingress,
                            const shared_ptr<pit::Entry>& pitEntry, const fib::Entry& fibEntry,
                            NamespaceInfo* measurementInfo, double r_val)
{
  // shared_ptr<Face> outFace = nullptr;
  nfd::face::Face *outFace = nullptr;
  double percSum = 0;

  // 这一块是为了检查所有接口的转发概率，至少要保证所有接口转发概率非0
  std::vector<fib::NextHop> eligbleHops;
  for (auto n: fibEntry.getNextHops()) {
    if (isNextHopEligible(ingress.face, interest, n, pitEntry, false, time::steady_clock::now())) {
      eligbleHops.push_back(n);
      percSum += measurementInfo->getforwPerc(n.getFace().getId());
    }
  }

  if (eligbleHops.size() < 1) {                   // 没有可用接口
    NFD_LOG_DEBUG("Blocked interest from face: " << ingress.face.getId() << " (no eligible faces)");
    return nullptr;
  }
  else if (eligbleHops.size() == 1) {             // 只有1个可用接口
    outFace = &eligbleHops.front().getFace();
  }
  else{                                           // 大于一个可用接口
    assert(percSum > 0);
    // PCON 算法在此处有个将转发概率写入文件的功能；
    double forwPerc = 0;
    for (auto n : eligbleHops) {
      forwPerc += measurementInfo->getforwPerc(n.getFace().getId()) / percSum;
      assert(forwPerc >= 0 && forwPerc <= 1.1);
      if (r_val < forwPerc) {
        outFace = &n.getFace();
        break;
      }
    }
  }
  return outFace;
}

/* 探测 */
void
PconStrategy::probeInterests(const FaceId outFaceId, const fib::NextHopList& nexthops,
                             shared_ptr<pit::Entry> pitEntry)
{

}

/*
 * afterContentStoreHit : 缓存命中后的操作
 *  - 需要重置HopCount缓存数据包中的HopCount标签
 */
void
PconStrategy::afterContentStoreHit(const Data& data, const FaceEndpoint& ingress,
                                    const shared_ptr<pit::Entry>& pitEntry)
{
  NFD_LOG_DEBUG("------------------------------------------------------------");
  NFD_LOG_DEBUG("[CacheHit] Prefix: " << data.getName());

  // ----- 重置HopCount标签
  data.setTag(make_shared<lp::HopCountTag>(0));                     
  return Strategy::afterContentStoreHit(data, ingress, pitEntry);
}


// void 
// PconStrategy::beforeExpirePendingInterest(shared_ptr<pit::Entry>& pitEntry)
// {
//   NFD_LOG_DEBUG("TODO")
// }


} // pcon
} // namespace fw
} // namespace nfd
