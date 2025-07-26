//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// index_scan_executor.cpp
//
// Identification: src/execution/index_scan_executor.cpp
//
// Copyright (c) 2015-19, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//
#include "execution/executors/index_scan_executor.h"

namespace bustub {
IndexScanExecutor::IndexScanExecutor(ExecutorContext *exec_ctx, const IndexScanPlanNode *plan)
    : AbstractExecutor(exec_ctx),
      plan_(plan),
      index_info_(exec_ctx_->GetCatalog()->GetIndex(plan_->index_oid_)),
      tree_(dynamic_cast<BPlusTreeIndexForTwoIntegerColumn *>(index_info_->index_.get())),
      it_(tree_->GetBeginIterator()) {}

void IndexScanExecutor::Init() {
  cursor_ = 0;
  it_ = tree_->GetBeginIterator();
}

auto IndexScanExecutor::Next(Tuple *tuple, RID *rid) -> bool {
  // project中order by不与where语句同时出现，这里根据pre_keys_是否为空简单判断是进行order by 还是 索引点查询
  if (!plan_->pred_keys_.empty()) {
    if (cursor_ >= plan_->pred_keys_.size()) {
      return false;
    }

    // 根据测试用例，pred_keys中为where中各个or判断中的常数，所以进行for循环遍历pred_keys查询b+树
    // 这里应该只考虑了比较简单的情形
    Schema dummy_schema({});
    std::vector<RID> tmp_result;
    while (cursor_ < plan_->pred_keys_.size()) {
      // 这部分对于index中key_tuple的理解需要深入，需要参考函数KeyFromTuple()
      auto key = plan_->pred_keys_[cursor_]->Evaluate(nullptr, dummy_schema);
      Tuple key_tuple({key}, &(index_info_->key_schema_));
      tree_->ScanKey(key_tuple, &tmp_result, exec_ctx_->GetTransaction());

      if (!tmp_result.empty()) {
        break;
      }
      cursor_++;
    }

    if (tmp_result.empty()) {
      std::cout << "emit false" << std::endl;
      return false;
    }
    *rid = tmp_result[0];
    auto table_info = exec_ctx_->GetCatalog()->GetTable(plan_->table_oid_);
    auto tuple_pair = table_info->table_->GetTuple(*rid);
    *tuple = tuple_pair.second;
    cursor_++;
    // std::cout << "emit tuple rid: " << rid << " cursor: " << cursor_ << std::endl;
    return true;
  }

  // 当为order by语句时
  if (it_.IsEnd()) {
    return false;
  }

  *rid = it_.operator*().second;
  auto table_info = exec_ctx_->GetCatalog()->GetTable(plan_->table_oid_);
  auto tuple_pair = table_info->table_->GetTuple(*rid);
  *tuple = tuple_pair.second;
  ++it_;
  return true;
}

}  // namespace bustub
