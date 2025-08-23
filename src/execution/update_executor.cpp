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

  auto txn_mgr = exec_ctx_->GetTransactionManager();
  int32_t updated_nums = 0;
  auto txn = exec_ctx_->GetTransaction();
  auto txn_tmp_ts = txn->GetTransactionTempTs();
  TupleMeta meta = {txn_tmp_ts, false};
  auto indexes = exec_ctx_->GetCatalog()->GetTableIndexes(table_info_->name_);

  // 与insert类似，所有tuple插入完返回true，然后再返回false
  while (child_executor_->Next(tuple, rid)) {
    // P3中 update 主要分为 delete 和 insert 两个部分

    // 先进行删除，直接更新TupleMeta的is_deleted_成员
    // meta.is_deleted_ = true;
    // table_info_->table_->UpdateTupleMeta(meta, *rid);

    // for (auto &index_info : indexes) {
    //   auto key = tuple->KeyFromTuple(table_info_->schema_, index_info->key_schema_,
    //   index_info->index_->GetKeyAttrs()); index_info->index_->DeleteEntry(key, *rid, exec_ctx_->GetTransaction());
    // }

    // 检测 写-写冲突
    auto base_meta = table_info_->table_->GetTupleMeta(*rid);
    if (base_meta.ts_ > txn->GetReadTs() && base_meta.ts_ != txn_tmp_ts) {
      // 还未实现Abort时，设置为Tainted
      txn->SetTainted();
      throw ExecutionException("in update_executor: write-write conflict");
    }

    // 再对tuple进行更新
    std::vector<Value> values{};
    values.reserve(GetOutputSchema().GetColumnCount());
    for (const auto &expr : plan_->target_expressions_) {
      values.push_back(expr->Evaluate(tuple, child_executor_->GetOutputSchema()));
    }
    Tuple new_tuple{values, &child_executor_->GetOutputSchema()};
    // 这里是为了在garbage collection中更方便
    tuple->SetRid(*rid);
    new_tuple.SetRid(*rid);

    // 生成undo_log
    // bool undo_link_valid_flag = true;
    // UndoLog log;
    // if (base_meta.ts_ == txn_tmp_ts) {
    //   // 如果原本tuple对应的最新undo_link为invalid，表示原本为insert，没有生成undo_log
    //   if (!txn_mgr->GetUndoLink(*rid).value().IsValid()) {
    //     undo_link_valid_flag = false;
    //   } else {
    //     log = GenerateUpdatedUndoLog(&(table_info_->schema_), tuple, &new_tuple,
    //     txn_mgr->GetUndoLog(*(txn_mgr->GetUndoLink(*rid))));
    //   }
    // } else {
    //   log = GenerateNewUndoLog(&(table_info_->schema_), tuple, &new_tuple, base_meta.ts_,
    //   *(txn_mgr->GetUndoLink(*rid)));
    // }

    // if (undo_link_valid_flag) {
    //   // 更新txn中的undo_logs和write_set，更新undo_link和tuple meta
    //   auto undo_link = txn->AppendUndoLog(std::move(log));
    //   txn->AppendWriteSet(table_info_->oid_, *rid);
    //   UpdateTupleAndUndoLink(txn_mgr, *rid, undo_link, table_info_->table_.get(), txn, meta, new_tuple, nullptr);
    // } else {
    //   table_info_->table_->UpdateTupleInPlace(meta, new_tuple, *rid, nullptr);
    // }

    // 生成undo_log
    UndoLog log;
    if (base_meta.ts_ == txn_tmp_ts) {
      // 如果原本tuple对应的最新undo_link为invalid，表示原本为insert，没有生成undo_log
      if (!txn_mgr->GetUndoLink(*rid).value().IsValid()) {
        table_info_->table_->UpdateTupleInPlace(meta, new_tuple, *rid, nullptr);
      } else {
        log = GenerateUpdatedUndoLog(&(table_info_->schema_), tuple, &new_tuple,
                                     txn_mgr->GetUndoLog(*(txn_mgr->GetUndoLink(*rid))));
        // 更新txn中的undo_logs中的对应undo_log
        txn->ModifyUndoLog(log.prev_version_.prev_log_idx_, log);
        table_info_->table_->UpdateTupleInPlace(meta, new_tuple, *rid, nullptr);
      }
    } else {
      log =
          GenerateNewUndoLog(&(table_info_->schema_), tuple, &new_tuple, base_meta.ts_, *(txn_mgr->GetUndoLink(*rid)));
      // 在txn中更新undo_logs和write_set，更新undo_link和tuple meta
      auto undo_link = txn->AppendUndoLog(std::move(log));
      txn->AppendWriteSet(table_info_->oid_, *rid);
      UpdateTupleAndUndoLink(txn_mgr, *rid, undo_link, table_info_->table_.get(), txn, meta, new_tuple, nullptr);
    }

    // 最后进行新tuple的插入
    // meta.is_deleted_ = true;
    // RID inserted_rid = table_info_->table_->InsertTuple(meta, new_tuple).value();

    // for (auto &index_info : indexes) {
    //   auto key =
    //       new_tuple.KeyFromTuple(table_info_->schema_, index_info->key_schema_, index_info->index_->GetKeyAttrs());
    //   index_info->index_->InsertEntry(key, inserted_rid, exec_ctx_->GetTransaction());
    // }

    updated_nums++;
  }

  completed_ = true;
  std::vector<Value> integer;
  integer.emplace_back(Value(INTEGER, updated_nums));
  *tuple = Tuple(integer, &GetOutputSchema());

  return true;
}

}  // namespace bustub
