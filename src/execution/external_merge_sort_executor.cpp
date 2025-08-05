//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// external_merge_sort_executor.cpp
//
// Identification: src/execution/external_merge_sort_executor.cpp
//
// Copyright (c) 2015-2024, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "execution/executors/external_merge_sort_executor.h"
#include <iostream>
#include <optional>
#include <vector>
#include "common/config.h"
#include "execution/plans/sort_plan.h"

namespace bustub {

template <size_t K>
ExternalMergeSortExecutor<K>::ExternalMergeSortExecutor(ExecutorContext *exec_ctx, const SortPlanNode *plan,
                                                        std::unique_ptr<AbstractExecutor> &&child_executor)
    : AbstractExecutor(exec_ctx), plan_(plan), cmp_(plan->GetOrderBy()), child_executor_(std::move(child_executor)) {}

template <size_t K>
void ExternalMergeSortExecutor<K>::Init() {
  // 如果此时runs不为空，则表示已经排序过，无需再重新排序
  // 这里主要是针对 nested loop join 中的情况
  if (!runs_.empty()) {
    // 重新初始化迭代器
    iter_ = runs_[0].Begin();
    return;
  }

  child_executor_->Init();
  runs_.clear();

  // PASS 0
  // 设置 PASS 0 中一个run中有几个sort_page
  int initial_page_cnt = 4;
  Tuple child_tuple{};
  RID rid;
  int tuple_size = static_cast<int>(sizeof(int32_t) + child_executor_->GetOutputSchema().GetInlinedStorageSize());
  int max_size = (BUSTUB_PAGE_SIZE - SORT_PAGE_HEADER_SIZE) / tuple_size;
  while (true) {
    std::vector<page_id_t> pages;
    page_id_t page_id = exec_ctx_->GetBufferPoolManager()->NewPage();
    pages.emplace_back(page_id);
    int cnt = 0;

    WritePageGuard page_guard = exec_ctx_->GetBufferPoolManager()->WritePage(page_id);
    auto sort_page = page_guard.AsMut<SortPage>();
    sort_page->Init(0, max_size, tuple_size);

    std::vector<SortEntry> entries;
    bool tuples_over = false;
    // 两个判断条件顺序不能反，不然会出错，多取到下一个tuple. 必须先判断cnt
    while (cnt < max_size * initial_page_cnt) {
      if (child_executor_->Next(&child_tuple, &rid)) {
        // sort_page->InsertTuple(child_tuple);
        entries.push_back(
            {GenerateSortKey(child_tuple, plan_->GetOrderBy(), GetOutputSchema()), std::move(child_tuple)});
        cnt++;
      } else {
        tuples_over = true;
        break;
      }
    }

    // cnt为maxsize时正好child_executor_的Next函数要返回false，此时的下一循环中，会进入该if
    if (cnt == 0 && tuples_over) {
      exec_ctx_->GetBufferPoolManager()->DeletePage(page_id);
      break;
    }

    std::sort(entries.begin(), entries.end(), cmp_);
    cnt = 0;
    for (const auto &entry : entries) {
      if (cnt == max_size) {
        page_id = exec_ctx_->GetBufferPoolManager()->NewPage();
        pages.emplace_back(page_id);
        page_guard = exec_ctx_->GetBufferPoolManager()->WritePage(page_id);
        sort_page = page_guard.AsMut<SortPage>();
        sort_page->Init(0, max_size, tuple_size);
        cnt = 0;
      }
      if (!sort_page->InsertTuple(entry.second)) {
        std::cout << "insert fail" << std::endl;
      }
      cnt++;
    }
    // 填充PASS 0的runs
    runs_.emplace_back(MergeSortRun(pages, exec_ctx_->GetBufferPoolManager()));
    // 所有tuple处理完毕
    if (tuples_over) {
      break;
    }
  }

  // // PASS 1,2,3...
  // while (runs_.size() > 1) {
  //   std::vector<MergeSortRun> new_runs;
  //   int n = runs_.size();
  //   for (int i = 0; i < n; i += 2) {
  //     std::vector<Tuple> tuples;

  //     // 考虑在右边界时，只有一个run的情况，即runs.size()为奇数时
  //     // 此时i + 1 == n
  //     if (i + 1 >= n) {
  //       new_runs.push_back(runs_[i]);
  //       break;
  //     }

  //     // merge part
  //     auto iter_a = runs_[i].Begin();
  //     auto iter_b = runs_[i + 1].Begin();
  //     while (iter_a != runs_[i].End() && iter_b != runs_[i + 1].End()) {
  //       SortEntry entry_a = {GenerateSortKey((*iter_a), plan_->GetOrderBy(), GetOutputSchema()), *iter_a};
  //       SortEntry entry_b = {GenerateSortKey((*iter_b), plan_->GetOrderBy(), GetOutputSchema()), *iter_b};
  //       if (cmp_(entry_a, entry_b)) {
  //         tuples.push_back(entry_a.second);
  //         ++iter_a;
  //       } else {
  //         tuples.push_back(entry_b.second);
  //         ++iter_b;
  //       }
  //     }

  //     // std::cout << "runs.size() 2 : " << runs_.size() << std::endl;
  //     while (iter_a != runs_[i].End()) {
  //       tuples.push_back(*iter_a);
  //       ++iter_a;
  //     }
  //     while (iter_b != runs_[i + 1].End()) {
  //       tuples.push_back(*iter_b);
  //       ++iter_b;
  //     }

  //     // std::cout << "tuples.size() : " << tuples.size() << std::endl;
  //     std::vector<page_id_t> pages_a = runs_[i].GetPages();
  //     std::vector<page_id_t> pages_b = runs_[i + 1].GetPages();
  //     std::vector<page_id_t> new_pages(pages_a.begin(), pages_a.end());
  //     new_pages.insert(new_pages.end(), pages_b.begin(), pages_b.end());
  //     // std::cout << "new_pages.size() : " << new_pages.size() << std::endl;
  //     // 用于tuples目前所在位置
  //     int idx = 0;
  //     for (auto page_id : new_pages) {
  //       auto page_guard = exec_ctx_->GetBufferPoolManager()->WritePage(page_id);
  //       auto sort_page = page_guard.AsMut<SortPage>();
  //       sort_page->Clear();

  //       int cnt = 0;
  //       while (cnt < max_size && idx < static_cast<int>(tuples.size())) {
  //         if (!sort_page->InsertTuple(tuples[idx++])) {
  //           std::cout << "insert fail" << std::endl;
  //         }
  //         cnt++;
  //       }
  //     }
  //     new_runs.emplace_back(MergeSortRun(new_pages, exec_ctx_->GetBufferPoolManager()));
  //   }

  //   runs_ = std::move(new_runs);
  // }

  // PASS 1,2,3...
  while (runs_.size() > 1) {
    std::vector<MergeSortRun> new_runs;
    int n = runs_.size();
    for (int i = 0; i < n; i += 2) {
      // 导致时间性能变差的一个原因，增加了copy次数
      // 而且这里假定了tuples可以全部放在一个内存中的数组中，不符合external merge sort考虑的情况
      // std::vector<Tuple> tuples;

      // 考虑在右边界时，只有一个run的情况，即runs.size()为奇数时
      // 此时i + 1 == n
      if (i + 1 >= n) {
        new_runs.push_back(runs_[i]);
        break;
      }

      // merge part
      std::vector<page_id_t> new_pages;
      auto iter_a = runs_[i].Begin();
      auto iter_b = runs_[i + 1].Begin();
      page_id_t new_page_id = exec_ctx_->GetBufferPoolManager()->NewPage();
      new_pages.emplace_back(new_page_id);
      auto page_guard = exec_ctx_->GetBufferPoolManager()->WritePage(new_page_id);
      auto sort_page = page_guard.AsMut<SortPage>();
      // 要记得初始化！
      sort_page->Init(0, max_size, tuple_size);

      int cnt = 0;
      while (iter_a != runs_[i].End() && iter_b != runs_[i + 1].End()) {
        // 如果当前的sort_page已经填满
        if (cnt >= max_size) {
          new_page_id = exec_ctx_->GetBufferPoolManager()->NewPage();
          new_pages.emplace_back(new_page_id);
          page_guard = exec_ctx_->GetBufferPoolManager()->WritePage(new_page_id);
          sort_page = page_guard.AsMut<SortPage>();
          sort_page->Init(0, max_size, tuple_size);
          // 记得刷新cnt
          cnt = 0;
        }

        SortEntry entry_a = {GenerateSortKey((*iter_a), plan_->GetOrderBy(), GetOutputSchema()), *iter_a};
        SortEntry entry_b = {GenerateSortKey((*iter_b), plan_->GetOrderBy(), GetOutputSchema()), *iter_b};
        if (cmp_(entry_a, entry_b)) {
          // tuples.push_back(entry_a.second);
          sort_page->InsertTuple(entry_a.second);
          ++iter_a;
        } else {
          // tuples.push_back(entry_b.second);
          sort_page->InsertTuple(entry_b.second);
          ++iter_b;
        }
        cnt++;
      }

      // std::cout << "runs.size() 2 : " << runs_.size() << std::endl;
      while (iter_a != runs_[i].End()) {
        // 如果当前的sort_page已经填满
        if (cnt >= max_size) {
          new_page_id = exec_ctx_->GetBufferPoolManager()->NewPage();
          new_pages.emplace_back(new_page_id);
          page_guard = exec_ctx_->GetBufferPoolManager()->WritePage(new_page_id);
          sort_page = page_guard.AsMut<SortPage>();
          sort_page->Init(0, max_size, tuple_size);
          // 记得刷新cnt
          cnt = 0;
        }

        // tuples.push_back(*iter_a);
        sort_page->InsertTuple(*iter_a);
        ++iter_a;
        cnt++;
      }
      while (iter_b != runs_[i + 1].End()) {
        // 如果当前的sort_page已经填满
        if (cnt >= max_size) {
          new_page_id = exec_ctx_->GetBufferPoolManager()->NewPage();
          new_pages.emplace_back(new_page_id);
          page_guard = exec_ctx_->GetBufferPoolManager()->WritePage(new_page_id);
          sort_page = page_guard.AsMut<SortPage>();
          sort_page->Init(0, max_size, tuple_size);
          // 记得刷新cnt
          cnt = 0;
        }

        // tuples.push_back(*iter_b);
        sort_page->InsertTuple(*iter_b);
        ++iter_b;
        cnt++;
      }

      // 删除原本使用的SortPage
      for (auto page_id : runs_[i].GetPages()) {
        exec_ctx_->GetBufferPoolManager()->DeletePage(page_id);
      }
      for (auto page_id : runs_[i + 1].GetPages()) {
        exec_ctx_->GetBufferPoolManager()->DeletePage(page_id);
      }

      new_runs.emplace_back(MergeSortRun(new_pages, exec_ctx_->GetBufferPoolManager()));
    }

    runs_ = std::move(new_runs);
  }

  if (!runs_.empty()) {
    // 排序完成后将iter迭代器设置为runs中唯一元素的Begin()迭代器
    iter_ = runs_[0].Begin();
  }
}

template <size_t K>
auto ExternalMergeSortExecutor<K>::Next(Tuple *tuple, RID *rid) -> bool {
  if (runs_.empty() || iter_ == runs_[0].End()) {
    return false;
  }
  *tuple = *iter_;
  *rid = tuple->GetRid();
  ++iter_;
  return true;
}

template class ExternalMergeSortExecutor<2>;

}  // namespace bustub
