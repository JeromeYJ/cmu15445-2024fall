//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// seq_scan_executor.cpp
//
// Identification: src/execution/seq_scan_executor.cpp
//
// Copyright (c) 2015-2021, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "execution/executors/seq_scan_executor.h"

namespace bustub {

SeqScanExecutor::SeqScanExecutor(ExecutorContext *exec_ctx, const SeqScanPlanNode *plan)
    : AbstractExecutor(exec_ctx),
      plan_(plan),
      it_(std::make_unique<TableIterator>(
          exec_ctx->GetCatalog()->GetTable(plan_->GetTableOid())->table_->MakeIterator())) {}

void SeqScanExecutor::Init() {
  // Initialize the iterator for the table heap
  it_ =
      std::make_unique<TableIterator>(exec_ctx_->GetCatalog()->GetTable(plan_->GetTableOid())->table_->MakeIterator());
}

auto SeqScanExecutor::Next(Tuple *tuple, RID *rid) -> bool {
  while (!it_->IsEnd()) {
    auto [meta, current_tuple] = it_->GetTuple();
    // 跳过已经被删除的元组
    if (meta.is_deleted_) {
      ++(*it_);
      continue;
    }

    // If there is a filter predicate, evaluate it
    if (plan_->filter_predicate_ != nullptr) {
      if (!plan_->filter_predicate_->Evaluate(&current_tuple, plan_->OutputSchema()).GetAs<bool>()) {
        ++(*it_);
        continue;
      }
    }

    *tuple = current_tuple;
    *rid = it_->GetRID();
    ++(*it_);
    return true;
  }

  return false;
}

}  // namespace bustub
