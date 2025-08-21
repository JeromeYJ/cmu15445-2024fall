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

  auto txn_mgr = exec_ctx_->GetTransactionManager();
  int32_t deleted_nums = 0;
  auto txn = exec_ctx_->GetTransaction();
  auto txn_tmp_ts = txn->GetTransactionTempTs();
  TupleMeta meta = {txn_tmp_ts, true};
  auto indexes = exec_ctx_->GetCatalog()->GetTableIndexes(table_info_->name_);

  // 与insert类似，所有tuple删除完返回true，然后再返回false
  while (child_executor_->Next(tuple, rid)) {
    // 检测 写-写冲突
    auto base_meta = table_info_->table_->GetTupleMeta(*rid);
    if (base_meta.ts_ > txn->GetReadTs() && base_meta.ts_ != txn_tmp_ts) {
      // 还未实现Abort时，设置为Tainted
      txn->SetTainted();
      throw ExecutionException("in delete_executor: write-write conflict");
    }

    // 生成undo_log
    // bool undo_link_valid_flag = true;
    UndoLog log;
    if (base_meta.ts_ == txn_tmp_ts) {
      // 如果原本tuple对应的最新undo_link为invalid，表示原本为insert，没有生成undo_log
      if (!txn_mgr->GetUndoLink(*rid).value().IsValid()) {
        // undo_link_valid_flag = false;
        table_info_->table_->UpdateTupleMeta(meta, *rid);
      } else {
        log = GenerateUpdatedUndoLog(&(table_info_->schema_), tuple, nullptr, txn_mgr->GetUndoLog(*(txn_mgr->GetUndoLink(*rid))));
        // 更新txn中的undo_logs中的对应undo_log
        txn->ModifyUndoLog(log.prev_version_.prev_log_idx_, log);
        table_info_->table_->UpdateTupleInPlace(meta, *tuple, *rid, nullptr);
      }
    } else {
      log = GenerateNewUndoLog(&(table_info_->schema_), tuple, nullptr, base_meta.ts_, *(txn_mgr->GetUndoLink(*rid)));
      // 在txn中更新undo_logs和write_set，更新undo_link和tuple meta
      auto undo_link = txn->AppendUndoLog(std::move(log));
      txn->AppendWriteSet(table_info_->oid_, *rid);
      UpdateTupleAndUndoLink(txn_mgr, *rid, undo_link, table_info_->table_.get(), txn, meta, *tuple, nullptr);
    }

    // table_info_->table_->UpdateTupleMeta(meta, *rid);

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
