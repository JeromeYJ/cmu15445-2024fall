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
  std::shared_ptr<IndexInfo> primary_index_info;

  // 如果是有主键索引，进行更新，则需要先将所有涉及到的tuple删除，存入将需要插入的new_tuple存入vector中，最后再一起插入
  std::vector<Tuple> new_tuples;

  // 与insert类似，所有tuple插入完返回true，然后再返回false
  while (child_executor_->Next(tuple, rid)) {
    // P3中 update 主要分为 delete 和 insert 两个部分
    // P4中有所不同

    // 检测 写-写冲突
    auto base_meta = table_info_->table_->GetTupleMeta(*rid);
    if (base_meta.ts_ > txn->GetReadTs() && base_meta.ts_ != txn_tmp_ts) {
      // 还未实现Abort时，设置为Tainted
      txn->SetTainted();
      throw ExecutionException("in update_executor: write-write conflict in first check phase");
    }

    // 对tuple进行更新
    std::vector<Value> values{};
    values.reserve(GetOutputSchema().GetColumnCount());
    for (const auto &expr : plan_->target_expressions_) {
      values.push_back(expr->Evaluate(tuple, child_executor_->GetOutputSchema()));
    }
    Tuple new_tuple{values, &child_executor_->GetOutputSchema()};
    // 这里是为了在garbage collection中更方便
    tuple->SetRid(*rid);
    new_tuple.SetRid(*rid);

    // 处理主键索引情况下的update
    bool process_in_primary_index = false;
    for (auto &index_info : indexes) {
      // 记得是用更新后的new_tuple求key
      // auto key = new_tuple.KeyFromTuple(table_info_->schema_, index_info->key_schema_,
      // index_info->index_->GetKeyAttrs()); std::vector<RID> result; 对primary key index的处理
      if (index_info->is_primary_key_) {
        /* 1. 先将所有要更新的tuple删除 */
        meta.is_deleted_ = true;
        UndoLog log;
        if (base_meta.ts_ == txn_tmp_ts) {
          // 如果原本tuple对应的最新undo_link为invalid，表示原本为insert，没有生成undo_log
          if (!txn_mgr->GetUndoLink(*rid).value().IsValid()) {
            // undo_link_valid_flag = false;
            table_info_->table_->UpdateTupleMeta(meta, *rid);
          } else {
            log = GenerateUpdatedUndoLog(&(table_info_->schema_), tuple, nullptr,
                                         txn_mgr->GetUndoLog(*(txn_mgr->GetUndoLink(*rid))));
            // 更新txn中的undo_logs中的对应undo_log
            txn->ModifyUndoLog((txn_mgr->GetUndoLink(*rid))->prev_log_idx_, log);
            if (!table_info_->table_->UpdateTupleInPlace(
                    meta, *tuple, *rid, [txn](const TupleMeta &meta, const Tuple &table, RID rid) {
                      return meta.ts_ <= txn->GetReadTs() || meta.ts_ == txn->GetTransactionTempTs();
                    })) {
              txn->SetTainted();
              throw ExecutionException("in insert_executor: primary key index conflict in check phase");
            }
          }
        } else if (base_meta.ts_ <= txn->GetReadTs()) {
          log =
              GenerateNewUndoLog(&(table_info_->schema_), tuple, nullptr, base_meta.ts_, *(txn_mgr->GetUndoLink(*rid)));
          // 在txn中更新undo_logs和write_set，更新undo_link和tuple meta
          auto undo_link = txn->AppendUndoLog(std::move(log));
          txn->AppendWriteSet(table_info_->oid_, *rid);
          if (!UpdateTupleAndUndoLink(
                  txn_mgr, *rid, undo_link, table_info_->table_.get(), txn, meta, *tuple,
                  [txn](const TupleMeta &meta, const Tuple &tuple, RID rid, std::optional<UndoLink>) {
                    return meta.ts_ <= txn->GetReadTs() || meta.ts_ == txn->GetTransactionTempTs();
                  })) {
            txn->SetTainted();
            throw ExecutionException("in insert_executor: primary key index conflict in check phase");
          }
        } else {
          txn->SetTainted();
          throw ExecutionException("in update_executor: primary key index conflict in delete phase");
        }

        new_tuples.emplace_back(new_tuple);

        primary_index_info = index_info;
        process_in_primary_index = true;
        break;
      }
    }

    // 主键索引处理之外的一般情况
    if (!process_in_primary_index) {
      // 生成undo_log
      UndoLog log;
      if (base_meta.ts_ == txn_tmp_ts) {
        // 如果原本tuple对应的最新undo_link为invalid，表示原本为insert，没有生成undo_log
        if (!txn_mgr->GetUndoLink(*rid).value().IsValid()) {
          if (!table_info_->table_->UpdateTupleInPlace(
                  meta, new_tuple, *rid, [txn](const TupleMeta &meta, const Tuple &table, RID rid) {
                    return meta.ts_ <= txn->GetReadTs() || meta.ts_ == txn->GetTransactionTempTs();
                  })) {
            txn->SetTainted();
            throw ExecutionException("in insert_executor: primary key index conflict in check phase");
          }
        } else {
          log = GenerateUpdatedUndoLog(&(table_info_->schema_), tuple, &new_tuple,
                                       txn_mgr->GetUndoLog(*(txn_mgr->GetUndoLink(*rid))));
          // 更新txn中的undo_logs中的对应undo_log
          txn->ModifyUndoLog(log.prev_version_.prev_log_idx_, log);
          if (!table_info_->table_->UpdateTupleInPlace(
                  meta, new_tuple, *rid, [txn](const TupleMeta &meta, const Tuple &table, RID rid) {
                    return meta.ts_ <= txn->GetReadTs() || meta.ts_ == txn->GetTransactionTempTs();
                  })) {
            txn->SetTainted();
            throw ExecutionException("in insert_executor: primary key index conflict in check phase");
          }
        }
      } else {
        log = GenerateNewUndoLog(&(table_info_->schema_), tuple, &new_tuple, base_meta.ts_,
                                 *(txn_mgr->GetUndoLink(*rid)));
        // 在txn中更新undo_logs和write_set，更新undo_link和tuple meta
        auto undo_link = txn->AppendUndoLog(std::move(log));
        txn->AppendWriteSet(table_info_->oid_, *rid);
        if (!UpdateTupleAndUndoLink(txn_mgr, *rid, undo_link, table_info_->table_.get(), txn, meta, new_tuple,
                                    [txn](const TupleMeta &meta, const Tuple &tuple, RID rid, std::optional<UndoLink>) {
                                      return meta.ts_ <= txn->GetReadTs() || meta.ts_ == txn->GetTransactionTempTs();
                                    })) {
          txn->SetTainted();
          throw ExecutionException("in insert_executor: primary key index conflict in check phase");
        }
      }
    }

    updated_nums++;
  }

  /* 2. (有primary key index的情况)再将更新后的tuple插入 */
  meta.is_deleted_ = false;
  bool insert_into_deleted;
  for (auto new_tuple : new_tuples) {
    insert_into_deleted = false;
    UndoLog log;
    std::vector<RID> result;
    // 记得是用更新后的new_tuple求key
    auto key = new_tuple.KeyFromTuple(table_info_->schema_, primary_index_info->key_schema_,
                                      primary_index_info->index_->GetKeyAttrs());
    primary_index_info->index_->ScanKey(key, &result, txn);
    // 若索引中已经存在key，则先判断是否为deleted元组，若是，则修改tuple为插入的tuple。否则事务abort / 设置为tainted
    if (!result.empty()) {
      *rid = result[0];
      new_tuple.SetRid(*rid);
      auto [base_meta, base_tuple] = table_info_->table_->GetTuple(*rid);
      if (base_meta.is_deleted_) {
        if (base_meta.ts_ <= txn->GetReadTs()) {
          log = GenerateNewUndoLog(&(table_info_->schema_), nullptr, &new_tuple, base_meta.ts_,
                                   *(txn_mgr->GetUndoLink(*rid)));
          // 在txn中更新undo_logs和write_set，更新undo_link和tuple meta
          auto undo_link = txn->AppendUndoLog(std::move(log));
          txn->AppendWriteSet(table_info_->oid_, *rid);
          if (!UpdateTupleAndUndoLink(
                  txn_mgr, *rid, undo_link, table_info_->table_.get(), txn, meta, new_tuple,
                  [txn](const TupleMeta &meta, const Tuple &tuple, RID rid, std::optional<UndoLink>) {
                    return meta.ts_ <= txn->GetReadTs() || meta.ts_ == txn->GetTransactionTempTs();
                  })) {
            txn->SetTainted();
            throw ExecutionException("in insert_executor: primary key index conflict in check phase");
          }
          insert_into_deleted = true;
        } else if (base_meta.ts_ == txn_tmp_ts) {
          // self modification case

          // 如果原本tuple对应的最新undo_link为invalid，表示tuple为本txn通过insert插入，没有生成undo_log
          // 所以之后本txn的更新操作保持没有undo_log
          if (!txn_mgr->GetUndoLink(*rid).value().IsValid()) {
            if (!table_info_->table_->UpdateTupleInPlace(
                    meta, new_tuple, *rid, [txn](const TupleMeta &meta, const Tuple &table, RID rid) {
                      return meta.ts_ <= txn->GetReadTs() || meta.ts_ == txn->GetTransactionTempTs();
                    })) {
              txn->SetTainted();
              throw ExecutionException("in insert_executor: primary key index conflict in check phase");
            }
          } else {
            log = GenerateUpdatedUndoLog(&(table_info_->schema_), nullptr, &new_tuple,
                                         txn_mgr->GetUndoLog(*(txn_mgr->GetUndoLink(*rid))));
            // 更新txn中的undo_logs中的对应undo_log
            txn->ModifyUndoLog((txn_mgr->GetUndoLink(*rid))->prev_log_idx_, log);
            if (!table_info_->table_->UpdateTupleInPlace(
                    meta, new_tuple, *rid, [txn](const TupleMeta &meta, const Tuple &table, RID rid) {
                      return meta.ts_ <= txn->GetReadTs() || meta.ts_ == txn->GetTransactionTempTs();
                    })) {
              txn->SetTainted();
              throw ExecutionException("in insert_executor: primary key index conflict in check phase");
            }
          }
          insert_into_deleted = true;
        } else {
          txn->SetTainted();
          throw ExecutionException("in update_executor(1): primary key index conflict in insert phase");
        }
      } else {
        txn->SetTainted();
        throw ExecutionException("in update_executor(2): primary key index conflict in insert phase");
      }
    }

    if (!insert_into_deleted) {
      *rid = table_info_->table_->InsertTuple(meta, new_tuple).value();
      // 在事务 write set 中加入插入tuple的rid
      txn->AppendWriteSet(plan_->GetTableOid(), *rid);
      // 更新txn mgr中的undo_link
      txn_mgr->UpdateUndoLink(*rid, std::make_optional(UndoLink{}), nullptr);

      if (!primary_index_info->index_->InsertEntry(key, *rid, txn)) {
        txn->SetTainted();
        throw ExecutionException("in update_executor(3): primary key index conflict in insert phase");
      }
    }
  }

  completed_ = true;
  std::vector<Value> integer;
  integer.emplace_back(Value(INTEGER, updated_nums));
  *tuple = Tuple(integer, &GetOutputSchema());

  return true;
}

}  // namespace bustub
