//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// external_merge_sort_executor.h
//
// Identification: src/include/execution/executors/external_merge_sort_executor.h
//
// Copyright (c) 2015-2024, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <memory>
#include <utility>
#include <vector>
#include "common/config.h"
#include "common/macros.h"
#include "execution/execution_common.h"
#include "execution/executors/abstract_executor.h"
#include "execution/plans/sort_plan.h"
#include "storage/table/tuple.h"

namespace bustub {

#define SORT_PAGE_HEADER_SIZE 12

/**
 * Page to hold the intermediate data for external merge sort.
 *
 * Only fixed-length data will be supported in Fall 2024.
 *
 * Sort Page Format:
 *    12
 * ----------
 * | HEADER |
 * ----------
 * ----------------------------------------------
 * |  Tuple(1)  |  Tuple(2)  | ... |  Tuple(n)  |
 * ----------------------------------------------
 *
 * HEADER Format:
 * ----------------------------------
 * | size_ | maxsize_ | tuple_size_ |
 * ----------------------------------
 *
 * Tuple Format (after serialization):
 *    4    schema.GetInlinedStorageSize()
 * --------------------------------------
 * | size |            data             |
 * --------------------------------------
 *
 * 关于内存与磁盘数据传输的工作都交给了buffer pool manager，包括脏页写回、磁盘页面读取等
 * 所以SortPage的功能设计不需要考虑序列化、反序列化等，只需要考虑在内存中的操作
 * 这也是数据库缓冲区buffer pool设计的巧妙之处
 */
class SortPage {
 public:
  /**
   * TODO: Define and implement the methods for reading data from and writing data to the sort
   * page. Feel free to add other helper methods.
   */
  // 参考 b plus tree page，删除默认constructor
  SortPage() = delete;
  SortPage(const SortPage &other) = delete;

  void Init(int size, int max_size, int tuple_size) {
    size_ = size;
    max_size_ = max_size;
    tuple_size_ = tuple_size;
    // data_为指向当前SortPage中元组区起始地址的指针
    // data_ = reinterpret_cast<char*>(this) + SORT_PAGE_HEADER_SIZE;
  }

  auto GetSize() const -> int { return size_; }

  auto GetMaxSize() const -> int { return max_size_; }

  auto IsFull() const -> bool { return size_ == max_size_; }

  auto InsertTuple(const Tuple &tuple) -> bool {
    if (IsFull()) {
      return false;
    }
    int offset = size_ * tuple_size_;
    tuple.SerializeTo(data_ + offset);
    size_++;
    return true;
  }

  auto GetTupleAt(int idx) const -> Tuple {
    int offset = idx * tuple_size_;
    Tuple tuple{};
    tuple.DeserializeFrom(data_ + offset);
    return tuple;
  }

  void Clear() {
    size_ = 0;
    //
  }

 private:
  /**
   * TODO: Define the private members. You may want to have some necessary metadata for
   * the sort page before the start of the actual data.
   */
  // 元数据
  int size_;
  int max_size_;
  int tuple_size_;
  // tuples 元组数据区
  char data_[];
};

/**
 * A data structure that holds the sorted tuples as a run during external merge sort.
 * Tuples might be stored in multiple pages, and tuples are ordered both within one page
 * and across pages.
 */
class MergeSortRun {
 public:
  MergeSortRun() = default;
  MergeSortRun(std::vector<page_id_t> pages, BufferPoolManager *bpm) : pages_(std::move(pages)), bpm_(bpm) {}

  auto GetPageCount() -> size_t { return pages_.size(); }

  auto GetPages() -> std::vector<page_id_t> { return pages_; }

  /** Iterator for iterating on the sorted tuples in one run. */
  class Iterator {
    friend class MergeSortRun;

   public:
    Iterator() = default;

    /**
     * Advance the iterator to the next tuple. If the current sort page is exhausted, move to the
     * next sort page.
     *
     * TODO: Implement this method.
     */
    auto operator++() -> Iterator & {
      auto sort_page = page_guard_.As<SortPage>();
      tuple_idx_++;
      if (tuple_idx_ >= sort_page->GetSize()) {
        pages_idx_++;
        tuple_idx_ = 0;
        if (pages_idx_ < static_cast<int>(run_->pages_.size())) {
          page_guard_ = run_->bpm_->ReadPage(run_->pages_[pages_idx_]);
        }
      }
      return *this;
    }

    /**
     * Dereference the iterator to get the current tuple in the sorted run that the iterator is
     * pointing to.
     *
     * TODO: Implement this method.
     */
    auto operator*() -> Tuple {
      auto sort_page = page_guard_.As<SortPage>();
      return sort_page->GetTupleAt(tuple_idx_);
    }

    /**
     * Checks whether two iterators are pointing to the same tuple in the same sorted run.
     *
     * TODO: Implement this method.
     */
    auto operator==(const Iterator &other) const -> bool {
      return run_ == other.run_ && pages_idx_ == other.pages_idx_ && tuple_idx_ == other.tuple_idx_;
    }

    /**
     * Checks whether two iterators are pointing to different tuples in a sorted run or iterating
     * on different sorted runs.
     *
     * TODO: Implement this method.
     */
    auto operator!=(const Iterator &other) const -> bool {
      return run_ != other.run_ || pages_idx_ != other.pages_idx_ || tuple_idx_ != other.tuple_idx_;
    }

   private:
    explicit Iterator(const MergeSortRun *run, size_t pages_idx, size_t tuple_idx)
        : run_(run), pages_idx_(pages_idx), tuple_idx_(tuple_idx) {
      if (pages_idx_ < static_cast<int>(run_->pages_.size())) {
        page_guard_ = run_->bpm_->ReadPage(run_->pages_[pages_idx_]);
      }
    }

    /** The sorted run that the iterator is iterating on. */
    [[maybe_unused]] const MergeSortRun *run_;

    /**
     * TODO: Add your own private members here. You may want something to record your current
     * position in the sorted run. Also feel free to add additional constructors to initialize
     * your private members.
     */
    // 目前所在的SortPage在pages_中的index
    int pages_idx_;
    // 目前所在SortPage内的tuple index
    int tuple_idx_;
    // 保存当前页面的ReadPageGuard，提高性能
    // 之前没有设置这个，性能很差
    ReadPageGuard page_guard_{};
  };

  /**
   * Get an iterator pointing to the beginning of the sorted run, i.e. the first tuple.
   *
   * TODO: Implement this method.
   */
  auto Begin() -> Iterator { return Iterator(this, 0, 0); }

  /**
   * Get an iterator pointing to the end of the sorted run, i.e. the position after the last tuple.
   *
   * TODO: Implement this method.
   */
  auto End() -> Iterator { return Iterator(this, pages_.size(), 0); }

 private:
  /** The page IDs of the sort pages that store the sorted tuples. */
  std::vector<page_id_t> pages_;
  /**
   * The buffer pool manager used to read sort pages. The buffer pool manager is responsible for
   * deleting the sort pages when they are no longer needed.
   */
  [[maybe_unused]] BufferPoolManager *bpm_;
};

/**
 * ExternalMergeSortExecutor executes an external merge sort.
 *
 * In Fall 2024, only 2-way external merge sort is required.
 */
template <size_t K>
class ExternalMergeSortExecutor : public AbstractExecutor {
 public:
  ExternalMergeSortExecutor(ExecutorContext *exec_ctx, const SortPlanNode *plan,
                            std::unique_ptr<AbstractExecutor> &&child_executor);

  /** Initialize the external merge sort */
  void Init() override;

  /**
   * Yield the next tuple from the external merge sort.
   * @param[out] tuple The next tuple produced by the external merge sort.
   * @param[out] rid The next tuple RID produced by the external merge sort.
   * @return `true` if a tuple was produced, `false` if there are no more tuples
   */
  auto Next(Tuple *tuple, RID *rid) -> bool override;

  /** @return The output schema for the external merge sort */
  auto GetOutputSchema() const -> const Schema & override { return plan_->OutputSchema(); }

 private:
  /** The sort plan node to be executed */
  const SortPlanNode *plan_;

  /** Compares tuples based on the order-bys */
  TupleComparator cmp_;

  /** TODO: You will want to add your own private members here. */
  std::unique_ptr<AbstractExecutor> child_executor_;

  std::vector<MergeSortRun> runs_;

  MergeSortRun::Iterator iter_{};
};

}  // namespace bustub
