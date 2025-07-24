/**
 * index_iterator.cpp
 */
#include <cassert>

#include "storage/index/index_iterator.h"

namespace bustub {

/*
 * NOTE: you can change the destructor/constructor method here
 * set your own input parameters
 */
INDEX_TEMPLATE_ARGUMENTS
INDEXITERATOR_TYPE::IndexIterator(BufferPoolManager *bpm, page_id_t page_id, int index)
    : bpm_(bpm), page_id_(page_id), index_(index), result_({KeyType{}, ValueType{}}) {
  if (page_id == INVALID_PAGE_ID) {
    return;
  }
  auto page_guard = bpm_->ReadPage(page_id_);
  auto leaf_page = page_guard.As<B_PLUS_TREE_LEAF_PAGE_TYPE>();
  result_.first = leaf_page->KeyAt(index_);
  result_.second = leaf_page->ValueAt(index_);
}

INDEX_TEMPLATE_ARGUMENTS
INDEXITERATOR_TYPE::~IndexIterator() = default;  // NOLINT

INDEX_TEMPLATE_ARGUMENTS
auto INDEXITERATOR_TYPE::IsEnd() -> bool { return index_ == -1; }

INDEX_TEMPLATE_ARGUMENTS
auto INDEXITERATOR_TYPE::operator*() -> std::pair<const KeyType &, const ValueType &> {
  // std::cout << "page_id: " << page_guard_.GetPageId() << " index: " << index_ << std::endl;
  return result_;
}

// 根据bootcamp中的例子，return的应该还是迭代器自己本身，所以需要直接修改迭代器自己的成员，最后 return *this
INDEX_TEMPLATE_ARGUMENTS
auto INDEXITERATOR_TYPE::operator++() -> INDEXITERATOR_TYPE & {
  index_++;
  auto page_guard = bpm_->ReadPage(page_id_);
  auto leaf_page = page_guard.As<B_PLUS_TREE_LEAF_PAGE_TYPE>();

  // 若index自增后，超过了当前页面的size大小，则通过next_page_id，更新信息
  if (index_ >= leaf_page->GetSize()) {
    page_id_t next_page_id = leaf_page->GetNextPageId();
    // 如果已经到了最后一个结点的最后一个键值对，则将迭代器设置为End()
    if (next_page_id == INVALID_PAGE_ID) {
      index_ = -1;
      page_id_ = next_page_id;
      return *this;
    }
    index_ = 0;
    page_id_ = next_page_id;

    page_guard = bpm_->ReadPage(page_id_);
    leaf_page = page_guard.As<B_PLUS_TREE_LEAF_PAGE_TYPE>();
  }

  result_ = {leaf_page->KeyAt(index_), leaf_page->ValueAt(index_)};
  return *this;
}

template class IndexIterator<GenericKey<4>, RID, GenericComparator<4>>;

template class IndexIterator<GenericKey<8>, RID, GenericComparator<8>>;

template class IndexIterator<GenericKey<16>, RID, GenericComparator<16>>;

template class IndexIterator<GenericKey<32>, RID, GenericComparator<32>>;

template class IndexIterator<GenericKey<64>, RID, GenericComparator<64>>;

}  // namespace bustub
