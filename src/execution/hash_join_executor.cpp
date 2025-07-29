//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// hash_join_executor.cpp
//
// Identification: src/execution/hash_join_executor.cpp
//
// Copyright (c) 2015-2021, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "execution/executors/hash_join_executor.h"

namespace bustub {

HashJoinExecutor::HashJoinExecutor(ExecutorContext *exec_ctx, const HashJoinPlanNode *plan,
                                   std::unique_ptr<AbstractExecutor> &&left_child,
                                   std::unique_ptr<AbstractExecutor> &&right_child)
    : AbstractExecutor(exec_ctx),
      plan_(plan),
      left_child_(std::move(left_child)),
      right_child_(std::move(right_child)) {
  if (!(plan->GetJoinType() == JoinType::LEFT || plan->GetJoinType() == JoinType::INNER)) {
    // Note for Fall 2024: You ONLY need to implement left join and inner join.
    throw bustub::NotImplementedException(fmt::format("join type {} not supported", plan->GetJoinType()));
  }
}

void HashJoinExecutor::Init() {
  left_child_->Init();
  right_child_->Init();

  ht_.clear();
  cursor_ = 0;
  Tuple tuple{};
  RID rid;
  // pipeline breaker的Init特点
  while (right_child_->Next(&tuple, &rid)) {
    ht_[GetRightJoinKey(&tuple)].tuples_.emplace_back(tuple);
  }
}

auto HashJoinExecutor::Next(Tuple *tuple, RID *rid) -> bool {
  while (true) {
    if (cursor_ == 0) {
      if (!left_child_->Next(&left_tuple_, rid)) {
        return false;
      }
      right_tuples_.clear();
    }

    // 为空，则表示刚刚更新left_tuple_，需要创建新的right_tuples_
    if (right_tuples_.empty()) {
      if (ht_.count(GetLeftJoinKey(&left_tuple_)) == 0) {
        // 若为left outer join
        if (plan_->GetJoinType() == JoinType::LEFT) {
          std::vector<Value> values;
          for (uint32_t i = 0; i < left_child_->GetOutputSchema().GetColumnCount(); i++) {
            values.emplace_back(left_tuple_.GetValue(&(left_child_->GetOutputSchema()), i));
          }
          for (uint32_t i = 0; i < right_child_->GetOutputSchema().GetColumnCount(); i++) {
            values.emplace_back(
                ValueFactory::GetNullValueByType(right_child_->GetOutputSchema().GetColumn(i).GetType()));
          }
          *tuple = Tuple(values, &GetOutputSchema());
          return true;
        }

        // 若为inner join
        cursor_ = 0;
        continue;
      }

      for (const auto &tuple : ht_[GetLeftJoinKey(&left_tuple_)].tuples_) {
        right_tuples_.emplace_back(tuple);
      }
    }

    if (cursor_ < right_tuples_.size()) {
      std::vector<Value> values;
      Tuple right_tuple = right_tuples_[cursor_];
      for (uint32_t i = 0; i < left_child_->GetOutputSchema().GetColumnCount(); i++) {
        values.emplace_back(left_tuple_.GetValue(&(left_child_->GetOutputSchema()), i));
      }
      for (uint32_t i = 0; i < right_child_->GetOutputSchema().GetColumnCount(); i++) {
        values.emplace_back(right_tuple.GetValue(&(right_child_->GetOutputSchema()), i));
      }
      *tuple = Tuple(values, &GetOutputSchema());
      cursor_++;
      return true;
    }

    // 当cursor_大于等于size时，到下一循环，更新right_child_
    cursor_ = 0;
  }
}

}  // namespace bustub
