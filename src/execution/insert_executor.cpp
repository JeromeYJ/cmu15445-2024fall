//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// insert_executor.cpp
//
// Identification: src/execution/insert_executor.cpp
//
// Copyright (c) 2015-2021, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include <memory>

#include "execution/executors/insert_executor.h"

namespace bustub {

InsertExecutor::InsertExecutor(ExecutorContext *exec_ctx, const InsertPlanNode *plan,
                               std::unique_ptr<AbstractExecutor> &&child_executor)
    : AbstractExecutor(exec_ctx), plan_(plan), child_executor_(std::move(child_executor)) {}

void InsertExecutor::Init() {
  // child executor 初始化
  child_executor_->Init();
}

auto InsertExecutor::Next([[maybe_unused]] Tuple *tuple, RID *rid) -> bool {
  // 所有tuple插入完毕，结束
  if (completed_) {
    return false;
  }

  int32_t inserted_num = 0;
  auto info = exec_ctx_->GetCatalog()->GetTable(plan_->table_oid_);
  auto indexes = exec_ctx_->GetCatalog()->GetTableIndexes(info->name_);
  auto table_oid = plan_->GetTableOid();
  auto txn_mgr = exec_ctx_->GetTransactionManager();
  auto txn = exec_ctx_->GetTransaction();

  // 设置插入的新tuple 的 TupleMeta
  TupleMeta meta = {txn->GetTransactionTempTs(), false};

  // insert执行中是将所有要插入的tuple一次性插完，然后返回true
  // 之后再进入这里的next函数时，返回false，结束insert执行
  while (child_executor_->Next(tuple, rid)) {
    /* 1. 因为有主键索引，所以优先进行主键索引的判断. 若是插入到一个deleted tuple，则在这一阶段完成插入，无需索引修改 */
    bool insert_into_deleted = false;
    for (auto &index_info : indexes) {
      auto key = tuple->KeyFromTuple(info->schema_, index_info->key_schema_, index_info->index_->GetKeyAttrs());
      std::vector<RID> result;
      // 添加primary key index的处理
      if (index_info->is_primary_key_) {
        index_info->index_->ScanKey(key, &result, txn);
        // 若索引中已经存在key，则先判断是否为deleted元组，若是，则修改tuple为插入的tuple。否则事务abort / 设置为tainted
        if (!result.empty()) {
          *rid = result[0];
          tuple->SetRid(*rid);
          auto [base_meta, base_tuple] = info->table_->GetTuple(*rid);
          if (base_meta.is_deleted_) {
            if (base_meta.ts_ <= txn->GetReadTs()) {
              auto log =
                  GenerateNewUndoLog(&(info->schema_), nullptr, tuple, base_meta.ts_, *(txn_mgr->GetUndoLink(*rid)));
              // 在txn中更新undo_logs和write_set，更新undo_link和tuple meta
              auto undo_link = txn->AppendUndoLog(std::move(log));
              txn->AppendWriteSet(info->oid_, *rid);
              if (!UpdateTupleAndUndoLink(
                      txn_mgr, *rid, undo_link, info->table_.get(), txn, meta, *tuple,
                      [txn](const TupleMeta &meta, const Tuple &tuple, RID rid, std::optional<UndoLink>) {
                        return meta.ts_ <= txn->GetReadTs() || meta.ts_ == txn->GetTransactionTempTs();
                      })) {
                txn->SetTainted();
                throw ExecutionException("in insert_executor: primary key index conflict in check phase");
              }
              insert_into_deleted = true;
            } else if (base_meta.ts_ == txn->GetTransactionTempTs()) {
              // self modification case, 这次出错了几次

              // 如果原本tuple对应的最新undo_link为invalid，表示tuple为本txn通过insert插入，没有生成undo_log
              // 所以之后本txn的更新操作保持没有undo_log
              if (!txn_mgr->GetUndoLink(*rid).value().IsValid()) {
                if (!info->table_->UpdateTupleInPlace(
                        meta, *tuple, *rid, [txn](const TupleMeta &meta, const Tuple &table, RID rid) {
                          return meta.ts_ <= txn->GetReadTs() || meta.ts_ == txn->GetTransactionTempTs();
                        })) {
                  txn->SetTainted();
                  throw ExecutionException("in insert_executor: primary key index conflict in check phase");
                }
              } else {
                auto log = GenerateUpdatedUndoLog(&(info->schema_), nullptr, tuple,
                                                  txn_mgr->GetUndoLog(*(txn_mgr->GetUndoLink(*rid))));
                // 更新txn中的undo_logs中的对应undo_log
                txn->ModifyUndoLog((txn_mgr->GetUndoLink(*rid))->prev_log_idx_, log);
                if (!info->table_->UpdateTupleInPlace(
                        meta, *tuple, *rid, [txn](const TupleMeta &meta, const Tuple &table, RID rid) {
                          return meta.ts_ <= txn->GetReadTs() || meta.ts_ == txn->GetTransactionTempTs();
                        })) {
                  txn->SetTainted();
                  throw ExecutionException("in insert_executor: primary key index conflict in check phase");
                }
              }
              insert_into_deleted = true;
            } else {
              txn->SetTainted();
              throw ExecutionException("in insert_executor: primary key index conflict in check phase");
            }
          } else {
            txn->SetTainted();
            throw ExecutionException("in insert_executor: primary key index conflict in check phase");
          }
        }
        break;
      }
    }

    /* 2. 若在主键没有完成元组插入，则进行一般性的元组插入 */
    if (!insert_into_deleted) {
      *rid = info->table_->InsertTuple(meta, *tuple).value();
      // 在事务 write set 中加入插入tuple的rid
      txn->AppendWriteSet(table_oid, *rid);
      // 更新txn mgr中的undo_link
      txn_mgr->UpdateUndoLink(*rid, std::make_optional(UndoLink{}), nullptr);

      /* 3. （一般性的）再进行各个索引的插入，其中有冗余键的set tainted */
      for (auto &index_info : indexes) {
        auto key = tuple->KeyFromTuple(info->schema_, index_info->key_schema_, index_info->index_->GetKeyAttrs());

        // 通过Insert再次检查是否有冗余key，因为前面过程中可能有其他事务插入了相同key
        if (!index_info->index_->InsertEntry(key, *rid, txn)) {
          txn->SetTainted();
          throw ExecutionException("in insert_executor: primary key index conflict in insert phase");
        }
      }
    }

    inserted_num++;
  }

  completed_ = true;
  std::vector<Value> integer;
  integer.emplace_back(Value(INTEGER, inserted_num));
  *tuple = Tuple(integer, &GetOutputSchema());

  return true;
}

}  // namespace bustub
