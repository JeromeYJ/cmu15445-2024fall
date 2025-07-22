//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// update_executor.cpp
//
// Identification: src/execution/update_executor.cpp
//
// Copyright (c) 2015-2021, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//
#include <memory>

#include "execution/executors/update_executor.h"

namespace bustub {

UpdateExecutor::UpdateExecutor(ExecutorContext *exec_ctx, const UpdatePlanNode *plan,
                               std::unique_ptr<AbstractExecutor> &&child_executor)
    : AbstractExecutor(exec_ctx),
      plan_(plan),
      table_info_(exec_ctx_->GetCatalog()->GetTable(plan->GetTableOid()).get()),
      child_executor_(std::move(child_executor)) {
  // As of Fall 2022, you DON'T need to implement update executor to have perfect score in project 3 / project 4.
}

void UpdateExecutor::Init() {
  // 有子结点的Init中都要记得初始化子结点
  child_executor_->Init();
}

auto UpdateExecutor::Next([[maybe_unused]] Tuple *tuple, RID *rid) -> bool {
  if (completed_) {
    return false;
  }

  int32_t updated_nums = 0;
  // p4中可能会要修改第一个成员值
  TupleMeta meta = {0, false};
  auto indexes = exec_ctx_->GetCatalog()->GetTableIndexes(table_info_->name_);

  // 与insert类似，所有tuple插入完返回true，然后再返回false
  while (child_executor_->Next(tuple, rid)) {
    // P3中 update 主要分为 delete 和 insert 两个部分

    // 先进行删除，直接更新TupleMeta的is_deleted_成员
    meta.is_deleted_ = true;
    table_info_->table_->UpdateTupleMeta(meta, *rid);

    for (auto &index_info : indexes) {
      auto key = tuple->KeyFromTuple(table_info_->schema_, index_info->key_schema_, index_info->index_->GetKeyAttrs());
      index_info->index_->DeleteEntry(key, *rid, exec_ctx_->GetTransaction());
    }

    // 再对tuple进行更新
    std::vector<Value> values{};
    values.reserve(GetOutputSchema().GetColumnCount());
    for (const auto &col : plan_->target_expressions_) {
      values.push_back(col->Evaluate(tuple, child_executor_->GetOutputSchema()));
    }
    Tuple new_tuple{values, &child_executor_->GetOutputSchema()};

    // 最后进行新tuple的插入
    meta.is_deleted_ = false;
    RID inserted_rid = table_info_->table_->InsertTuple(meta, new_tuple).value();

    for (auto &index_info : indexes) {
      auto key =
          new_tuple.KeyFromTuple(table_info_->schema_, index_info->key_schema_, index_info->index_->GetKeyAttrs());
      index_info->index_->InsertEntry(key, inserted_rid, exec_ctx_->GetTransaction());
    }

    updated_nums++;
  }

  completed_ = true;
  std::vector<Value> integer;
  integer.emplace_back(Value(INTEGER, updated_nums));
  *tuple = Tuple(integer, &GetOutputSchema());

  return true;
}

}  // namespace bustub
