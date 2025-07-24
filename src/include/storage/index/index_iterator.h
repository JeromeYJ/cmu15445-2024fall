//===----------------------------------------------------------------------===//
//
//                         CMU-DB Project (15-445/645)
//                         ***DO NO SHARE PUBLICLY***
//
// Identification: src/include/index/index_iterator.h
//
// Copyright (c) 2018, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//
/**
 * index_iterator.h
 * For range scan of b+ tree
 */
#pragma once
#include <utility>
#include "storage/page/b_plus_tree_leaf_page.h"

namespace bustub {

#define INDEXITERATOR_TYPE IndexIterator<KeyType, ValueType, KeyComparator>

INDEX_TEMPLATE_ARGUMENTS
class IndexIterator {
 public:
  // you may define your own constructor based on your member variables
  IndexIterator(BufferPoolManager *bpm, page_id_t page_id, int index);
  ~IndexIterator();  // NOLINT

  auto IsEnd() -> bool;

  // 这里返回类型pair中类型参数使用引用的原因是：避免不必要的拷贝
  // 在每次读取first和second时，若不使用引用，则会每次都拷贝两者，造成额外开销
  // 使用引用，则不会拷贝，每次读取相同对象
  auto operator*() -> std::pair<const KeyType &, const ValueType &>;

  auto operator++() -> IndexIterator &;

  auto operator==(const IndexIterator &itr) const -> bool {
    return ((index_ == -1 && itr.index_ == -1) || (page_id_ == itr.page_id_ && index_ == itr.index_));
  }

  auto operator!=(const IndexIterator &itr) const -> bool { return (page_id_ != itr.page_id_ || index_ != itr.index_); }

 private:
  // add your own private member variables here
  BufferPoolManager *bpm_;
  // 之前这里用pageGuard作为成员，导致了意想不到的死锁
  // ReadPageGuard page_guard_;
  page_id_t page_id_;
  int index_;
  std::pair<KeyType, ValueType> result_;
};

}  // namespace bustub
