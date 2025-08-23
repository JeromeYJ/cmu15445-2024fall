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

  // 设置插入的新tuple 的 TupleMeta
  TupleMeta meta = {exec_ctx_->GetTransaction()->GetTransactionTempTs(), false};

  // insert执行中是将所有要插入的tuple一次性插完，然后返回true
  // 之后再进入这里的next函数时，返回false，结束insert执行
  while (child_executor_->Next(tuple, rid)) {
    // 因为有主键索引，所以优先进行索引的判断
    for (auto &index_info : indexes) {
      auto key = tuple->KeyFromTuple(info->schema_, index_info->key_schema_, index_info->index_->GetKeyAttrs());
      std::vector<RID> result;
      // 添加primary key index的处理
      if (index_info->is_primary_key_) {
        index_info->index_->ScanKey(key, &result, exec_ctx_->GetTransaction());
        // 若索引中已经存在key，则则事务abort / 设置为tainted
        if (!result.empty()) {
          exec_ctx_->GetTransaction()->SetTainted();
          throw ExecutionException("in insert_executor: primary key index conflict in check phase");
        }
      }
      
      // 函数返回插入tuple的RID
      *rid = info->table_->InsertTuple(meta, *tuple).value();
      // 在事务 write set 中加入插入tuple的rid
      exec_ctx_->GetTransaction()->AppendWriteSet(table_oid, *rid);
      // 更新txn mgr中的undo_link
      exec_ctx_->GetTransactionManager()->UpdateUndoLink(*rid, std::make_optional(UndoLink{}), nullptr);

      if (index_info->is_primary_key_ && !index_info->index_->InsertEntry(key, *rid, exec_ctx_->GetTransaction())) {
        exec_ctx_->GetTransaction()->SetTainted();
        throw ExecutionException("in insert_executor: primary key index conflict in insert phase");
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
