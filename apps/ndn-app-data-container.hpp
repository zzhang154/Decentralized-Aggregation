/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/**
 * Author : z2h
 * Last modified : 22-06-05
 * brief : APP 数据容器 (自定义)
 *  - 数据管理和计算
 **/

#ifndef NDN_APP_DATA_CONTAINER_H
#define NDN_APP_DATA_CONTAINER_H

#include "ns3/ndnSIM/model/ndn-common.hpp"

#include "ndn-consumer.hpp"

namespace ns3 {
namespace ndn {

/**********************************************************************************
 **                        SlideWindowContainer 滑动窗口（容器）
 ** -----------------------------------------------------------------------------
 ** - deque基础上的滑动窗口容器
 **   - 队列管理
 **   - 不支持数值运算  
 **********************************************************************************/
template<class T>
class SlideWindowContainer
{
public:
  SlideWindowContainer(uint64_t window_size);

  void 
  insert(T item);                       // 添加记录

public:    // 状态管理
  void clear()
  {                          
    m_winQueue.clear();
  }

  bool isEmpty() const
  {
    return m_winQueue.empty();
  }

  bool isFull() const                   // 是否满了
  {
    return (m_winQueue.size() == m_winSize);
  }

  int getSize() const
  {
    return m_winQueue.size();
  }

public:       // 获取元素
  const T& 
  getFront() const                  // 获取头部值
  {
    return m_winQueue.front();
  }

  const T& 
  getBack() const                   // 获取尾部值
  {
    return m_winQueue.back();
  }

  typename::std::deque<T>::const_iterator
  getBegin() const
  {
    return m_winQueue.cbegin();
  }

  typename::std::deque<T>::const_iterator
  getEnd() const
  {
    return m_winQueue.cend();
  }

private:                            // 队列管理
  void insertBack(T item);

  void insertFront(T item);

private:      // 内部数据
  std::deque<T> m_winQueue;
  uint64_t m_winSize;
};


/**********************************************************************************
 **                               SlideMapContainer 滑动表
 ** -----------------------------------------------------------------------------
 ** - 多个滑动窗口
 **********************************************************************************/
template<class ValT>
class SlideMapContainer
{
public:
  SlideMapContainer(uint64_t window_size);

  void
  insert(uint64_t key_val, ValT item);               // 插入记录

public:
  shared_ptr<SlideWindowContainer<ValT>>
  getEntry(uint64_t key_val);

private:
  double m_winSize;

private:
  std::map<uint64_t, shared_ptr<SlideWindowContainer<ValT>>> m_winMap;
};


/**********************************************************************************
 **                        slidewindow 滑动窗口 (基本类型double)
 ** -----------------------------------------------------------------------------
 ** - 提供数值运算支持
 **********************************************************************************/
class SlideWindow : public SlideWindowContainer<double>
{
public:
  SlideWindow(uint64_t window_size);

  double
  getBack() const;

  double
  getFront() const;

  double 
  getDiff() const;

  double 
  getSum() const;

  double
  getMax() const;

  double
  getMin() const;

  double 
  getAverage() const;

  double 
  getChangeRate() const;

  double 
  getRofChangeRate() const;

  double
  getVariationDeviation() const;

  double 
  getStandardDeviation() const;

  // bool
  // HasSingleElem() const;

  // double 
  // getMost() const;
};

/**********************************************************************************
 **                        SlideWindowForInt 存储Int的滑动窗口
 ** -----------------------------------------------------------------------------
 ** - 提供数值运算支持
 **********************************************************************************/
class SlideWindowForInt : public SlideWindowContainer<uint64_t>
{
public:
  SlideWindowForInt(uint64_t window_size);

  bool
  HasSingleElem() const;

  uint64_t 
  getMost() const;

  void
  test(uint64_t val);
};

/**********************************************************************************
 **                        SlideWindowForPair 存储Pair的滑动窗口
 ** -----------------------------------------------------------------------------
 ** - 提供数值运算支持
 **********************************************************************************/
class SlideWindowForPair : public SlideWindowContainer<std::pair<double, double>>
{
public:
  SlideWindowForPair(uint64_t window_size);

  std::pair<double, double>
  getMax() const;

  std::pair<double, double>
  getMin() const;
};



/**********************************************************************************
 **                             SlideMap
 ** -----------------------------------------------------------------------------
 ** - 提供数值运算支持
 **********************************************************************************/
class SlideMap
{
public:
  SlideMap(uint64_t window_size);

  void
  insert(uint64_t key_val, double item);      // 插入记录

  void
  insertCover(uint64_t key_val, double item); // 覆盖型插入

  void
  insertWhole(double item);                   // 全部插入

  void
  clearExcept(uint64_t key_val);              // 部分清除

public:
  shared_ptr<SlideWindow>
  getEntry(uint64_t key_val) const;

  double
  getEntryBack(uint64_t key_val) const;

public:
  bool
  has(uint64_t key_val) const;

public:
  double
  getSum() const;                   // 获取所有记录中所有元素的和

  double
  getMaxInLatest() const;           // 获得最新元素的最大值

  double
  getSumInTails() const;            // 尾部求和

  double
  getSumInHeads() const;            // 头部求和

  double
  getMaxInTails() const;            // 尾部最大值

  double
  getMaxInHeads() const;            // 头部最大值

public:     // 项接口
  double
  getDiff(uint64_t key_val) const;

  double 
  getSum(uint64_t key_val) const;

  double
  getAverage(uint64_t key_val) const;

  double
  getChangeRate(uint64_t key_val) const;

  double
  getRofChangeRate(uint64_t key_val) const;

public:
  void
  show() const;

private:
  double m_winSize;
  std::map<uint64_t, shared_ptr<SlideWindow>> m_winMap;
};




} // namespace ndn
} // namespace ns3

#endif  // NDN_APP_DATA_CONTAINER_H


