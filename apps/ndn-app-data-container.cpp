/*
 * Author : z2h
 * Last modified : 22-11-14
 * brief : DPCCP帮助类
 **/

#include "ndn-app-data-container.hpp"

#include <numeric>
#include <math.h>

NS_LOG_COMPONENT_DEFINE("ndn.AppDataContainer");

namespace ns3 {
namespace ndn {

/**********************************************************************************
 **                                SlideWindowContainer 滑动窗口
 **********************************************************************************/

template<class T>
SlideWindowContainer<T>::SlideWindowContainer(uint64_t window_size)
  : m_winSize(window_size)
{
}

/*****************************************
 * Public Method : 队列管理
 *  - 保持固定长度
 *****************************************/
/* 插入记录 */
template<class T>
void SlideWindowContainer<T>::insert(T item)                 //添加记录
{
  this->insertBack(item);
}

/* 插入记录（从队头插入） */
template<class T>
void SlideWindowContainer<T>::insertFront(T item)            // 添加记录(队头)
{
  m_winQueue.push_front(item);
  if (m_winQueue.size() <= m_winSize) { return; }
  m_winQueue.pop_back();
}

/* 插入操作（队尾）*/
template<class T>
void SlideWindowContainer<T>::insertBack(T item)
{
  m_winQueue.push_back(item);
  if (m_winQueue.size() <= m_winSize) { return; }
  m_winQueue.pop_front();
}


/**********************************************************************************
 **                               SlideMapContainer
 ** -----------------------------------------------------------------------------
 ** 滑动表容器
 **********************************************************************************/
template<class ValT>
SlideMapContainer<ValT>::SlideMapContainer(uint64_t window_size)
  : m_winSize(window_size)
{
}

/* 插入 */
template<class ValT>
void SlideMapContainer<ValT>::insert(uint64_t key_val, ValT item)
{
  if (m_winMap.find(key_val) == m_winMap.end()){
    auto new_ptr = make_shared<SlideWindowContainer<ValT>>(m_winSize);
    m_winMap.insert(std::pair<uint64_t, shared_ptr<SlideWindowContainer<ValT>>>(key_val, new_ptr));
  }
  m_winMap.at(key_val)->insert(item);
}

/* 查找 */
template<class ValT>
shared_ptr<SlideWindowContainer<ValT>>
SlideMapContainer<ValT>::getEntry(uint64_t key_val)
{
  auto val_iter = m_winMap.find(key_val);
  if (val_iter == m_winMap.end()) {return nullptr;}
  return val_iter->second;
}


/**********************************************************************************
 **                         slidewindow 滑动窗口
 ** -----------------------------------------------------------------------------
 ** - 增加数值运算支持 —— double类型
 **********************************************************************************/
SlideWindow::SlideWindow(uint64_t window_size)
  : SlideWindowContainer(window_size)
{
}

double
SlideWindow::getBack() const
{
  if (isEmpty()) { return 0.0; }
  return SlideWindowContainer::getBack();
}

double
SlideWindow::getFront() const 
{
  if (isEmpty()) { return 0.0; }
  return SlideWindowContainer::getFront();
}


/* 求差 */
double
SlideWindow::getDiff() const
{
  if (isEmpty()) {return 0.0;}
  return static_cast<double>(getBack() - getFront());
}

/* 求和 */
double
SlideWindow::getSum() const
{
  return std::accumulate(getBegin(), getEnd(), 0.0);
}

double
SlideWindow::getMax() const
{
  auto res = std::max_element(getBegin(), getEnd(), \
  [](const auto &a, const auto &b) {
    return (a < b);
  });
  return *res;
}

double
SlideWindow::getMin() const
{
  auto res = std::min_element(getBegin(), getEnd(), \
  [](const auto &a, const auto &b) {
    return (a < b);
  });
  return *res;
}

/* 求平均 */
double
SlideWindow::getAverage() const
{
  if(isEmpty()){ return 0.0;}
  return (getSum() / getSize());
}

/* 变化率 */
double
SlideWindow::getChangeRate() const
{
  if (isEmpty()) {return 0.0;}
  return (getDiff() / getSize());
}

/* 变化率倒数 */
double
SlideWindow::getRofChangeRate() const
{
  if (getDiff() == 0.0) {return 0.0;}
  return (getSize() / getDiff());
}

/* 标准差 */
double 
SlideWindow::getStandardDeviation() const
{
  return std::sqrt(this->getVariationDeviation());
}

/* 方差 */
double
SlideWindow::getVariationDeviation() const
{
  auto size = this->getSize();
  if (size < 2) {return 0.0;}
  const auto mean = this->getAverage();
  auto square_mean = std::accumulate(getBegin(), getEnd(), 0.0,
  [&mean](double acc, const double x){
    return acc + std::pow((x - mean), 2);
  });
  return (square_mean / (size - 1));
}



/**********************************************************************************
 **                        SlideWindowForInt 存储Int的滑动窗口
 ** -----------------------------------------------------------------------------
 ** - 提供数值运算支持
 **********************************************************************************/
SlideWindowForInt::SlideWindowForInt(uint64_t window_size)
  : SlideWindowContainer(window_size)
{
}

/* 判断是否只有一个元素 */
bool
SlideWindowForInt::HasSingleElem() const
{
  if (this->isEmpty()) { return false;}
  auto head_val = this->getFront();
  
  std::deque<uint64_t>::const_iterator it;
  for (it=getBegin(); it!=getEnd(); it++) {
    if (*it != head_val) { return false; }
  }
  return true;
}

/* 获得出现次数最多的元素 */
uint64_t 
SlideWindowForInt::getMost() const
{
  uint64_t res = 0;
  int num = 0;
  std::map<uint64_t, int> cnt_map;

  std::deque<uint64_t>::const_iterator it;
  for (it=getBegin(); it!=getEnd(); it++) {
    if (cnt_map.find(*it) != cnt_map.end()) {
      cnt_map[*it]++;
    } else {
      cnt_map.insert(std::pair<uint64_t, int>(*it, 1));
    }
    if (cnt_map[*it] > num) {
      num = cnt_map[*it];
      res = *it;
    }
  }
  return res;
}

void
SlideWindowForInt::test(uint64_t val)
{
  this->insert(val);
}


/**********************************************************************************
 **                        SlideWindowForPair 存储Pair的滑动窗口
 ** -----------------------------------------------------------------------------
 ** - 提供数值运算支持
 **********************************************************************************/
SlideWindowForPair::SlideWindowForPair(uint64_t window_size)
  : SlideWindowContainer(window_size)
{
}

/* 查找最大值 */
std::pair<double, double>
SlideWindowForPair::getMax() const
{
  auto res = std::max_element(getBegin(), getEnd(), \
  [](const auto &a, const auto &b) {
    return (a.first < b.first);
  });
  return *res;
}

/* 查找最小值 */
std::pair<double, double>
SlideWindowForPair::getMin() const
{
  auto res = std::min_element(getBegin(), getEnd(), \
  [](const auto &a, const auto &b) {
    return (a.first < b.first);
  });
  return *res;
}


/**********************************************************************************
 **                        slideMap 滑动表 
 ** -----------------------------------------------------------------------------
 **********************************************************************************/
SlideMap::SlideMap(uint64_t window_size)
  : m_winSize(window_size)
{
}

/* 插入 */
void 
SlideMap::insert(uint64_t key_val, double item)
{
  if (m_winMap.find(key_val) == m_winMap.end()){
    auto new_ptr = make_shared<SlideWindow>(m_winSize);
    m_winMap.insert(std::pair<uint64_t, shared_ptr<SlideWindow>>(key_val, new_ptr));
  }
  m_winMap.at(key_val)->insert(item);
}

void
SlideMap::insertCover(uint64_t key_val, double item)
{
  if (m_winMap.find(key_val) == m_winMap.end()){
    auto new_ptr = make_shared<SlideWindow>(m_winSize);
    m_winMap.insert(std::pair<uint64_t, shared_ptr<SlideWindow>>(key_val, new_ptr));
  }
  for(auto it : m_winMap) {
    if (it.first <= key_val){
      m_winMap.at(it.first)->insert(item);
    }
  }
}

void
SlideMap::insertWhole(double item)
{
  for(auto it : m_winMap) {
    m_winMap.at(it.first)->insert(item);
  }
}

void
SlideMap::clearExcept(uint64_t key_val)
{
  for(auto it : m_winMap) {
    if (it.first != key_val) { m_winMap.at(it.first)->clear(); }
  }
}


bool
SlideMap::has(uint64_t key_val) const
{
  return (m_winMap.find(key_val) != m_winMap.end());
}


/* 查找 */
shared_ptr<SlideWindow>
SlideMap::getEntry(uint64_t key_val) const
{
  auto val_iter = m_winMap.find(key_val);
  if (val_iter == m_winMap.end()) {return nullptr;}
  return val_iter->second;
}

/* 查找 */
double
SlideMap::getEntryBack(uint64_t key_val) const
{
  auto val_iter = m_winMap.find(key_val);
  if (val_iter == m_winMap.end()) {return 0;}
  return val_iter->second->getBack();
}

/* 求和 */
double
SlideMap::getSum() const
{
  auto sum = std::accumulate(m_winMap.begin(), m_winMap.end(), 0.0, \
  [] (const double &total, auto it) {
    return total + it.second->getSum();
  });
  return sum;
}

/* 尾部值求和 */
double
SlideMap::getSumInTails() const
{
  auto sum = std::accumulate(m_winMap.begin(), m_winMap.end(), 0.0, \
  [] (const double &total, auto it) {
    return total + it.second->getBack();
  });
  return sum;
}

/* 头部值求和 */
double
SlideMap::getSumInHeads() const
{
  auto sum = std::accumulate(m_winMap.begin(), m_winMap.end(), 0.0, \
  [] (const double &total, auto it) {
    return total + it.second->getFront();
  });
  return sum;
}

/* 获得最新元素的最大值 (尾部最大值)*/
double
SlideMap::getMaxInLatest() const           // 获得最新元素的最大值
{
  return this->getMaxInTails();
}


/* 尾部最大值 */
double
SlideMap::getMaxInTails() const
{
  auto res = std::max_element(m_winMap.begin(), m_winMap.end(), \
  [](const auto &a, const auto &b) {
    return (a.second->getBack() < b.second->getBack());
  });
  return res->second->getBack();
}

/* 头部最大值 */
double
SlideMap::getMaxInHeads() const
{
  auto res = std::max_element(m_winMap.begin(), m_winMap.end(), \
  [](const auto &a, const auto &b) {
    return (a.second->getFront() < b.second->getFront());
  });
  return res->second->getFront();
}

/* 返回变化率倒数 */
double
SlideMap::getRofChangeRate(uint64_t key_val) const
{
  auto map_entry = this->getEntry(key_val);
  if (map_entry == nullptr) {return 0.0; }
  return map_entry->getRofChangeRate();
}

void
SlideMap::show() const
{
  NS_LOG_DEBUG("[SlideMap] Show SlideMap");
  for(auto it : m_winMap) {
    NS_LOG_DEBUG("[SlideMap] key: " << it.first << " size: " << it.second->getSize() << " end: " << it.second->getBack());
  }
}

/**********************************************************************************
 **                            模板函数 特化
 **********************************************************************************/

/* std::pair<double, double> insert ref*/
void PairInsertRef(SlideWindowContainer<std::pair<double, double>> &slide_win)
{
  slide_win.insert(std::pair<double, double>(0.0, 0.0));
}


}
}