//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// delete_executor.cpp
//
// Identification: src/execution/delete_executor.cpp
//
// Copyright (c) 2015-2021, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include <memory>

#include "execution/executors/delete_executor.h"

namespace bustub {

DeleteExecutor::DeleteExecutor(ExecutorContext *exec_ctx, const DeletePlanNode *plan,
                               std::unique_ptr<AbstractExecutor> &&child_executor)
    : AbstractExecutor(exec_ctx),
      plan_(plan),
      table_info_(exec_ctx_->GetCatalog()->GetTable(plan->GetTableOid()).get()),
      child_executor_(std::move(child_executor)) {}

void DeleteExecutor::Init() { child_executor_->Init(); }

auto DeleteExecutor::Next([[maybe_unused]] Tuple *tuple, RID *rid) -> bool {
  if (completed_) {
    return false;
  }

  int32_t deleted_nums = 0;
  // p4中可能会要修改第一个成员值
  TupleMeta meta = {0, false};
  auto indexes = exec_ctx_->GetCatalog()->GetTableIndexes(table_info_->name_);

  // 与insert类似，所有tuple插入完返回true，然后再返回false
  while (child_executor_->Next(tuple, rid)) {
    meta.is_deleted_ = true;
    table_info_->table_->UpdateTupleMeta(meta, *rid);

    for (auto &index_info : indexes) {
      auto key = tuple->KeyFromTuple(table_info_->schema_, index_info->key_schema_, index_info->index_->GetKeyAttrs());
      index_info->index_->DeleteEntry(key, *rid, exec_ctx_->GetTransaction());
    }

    deleted_nums++;
  }

  completed_ = true;
  std::vector<Value> integer;
  integer.emplace_back(Value(INTEGER, deleted_nums));
  *tuple = Tuple(integer, &GetOutputSchema());

  return true;
}

}  // namespace bustub
