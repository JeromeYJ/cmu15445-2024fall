//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// nested_loop_join_executor.cpp
//
// Identification: src/execution/nested_loop_join_executor.cpp
//
// Copyright (c) 2015-2021, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "execution/executors/nested_loop_join_executor.h"
#include "binder/table_ref/bound_join_ref.h"
#include "common/exception.h"

namespace bustub {

NestedLoopJoinExecutor::NestedLoopJoinExecutor(ExecutorContext *exec_ctx, const NestedLoopJoinPlanNode *plan,
                                               std::unique_ptr<AbstractExecutor> &&left_executor,
                                               std::unique_ptr<AbstractExecutor> &&right_executor)
    : AbstractExecutor(exec_ctx),
      plan_(plan),
      left_executor_(std::move(left_executor)),
      right_executor_(std::move(right_executor)) {
  if (!(plan->GetJoinType() == JoinType::LEFT || plan->GetJoinType() == JoinType::INNER)) {
    // Note for 2023 Fall: You ONLY need to implement left join and inner join.
    throw bustub::NotImplementedException(fmt::format("join type {} not supported", plan->GetJoinType()));
  }
}

void NestedLoopJoinExecutor::Init() {
  left_executor_->Init();
  right_executor_->Init();
}

auto NestedLoopJoinExecutor::Next(Tuple *tuple, RID *rid) -> bool {
  while (true) {
    // 判断当前处理的左表tuple是否已经与所有右表tuple比较过
    // 若是，则更新left_tuple_
    if (is_left_tuple_over_) {
      if (!left_executor_->Next(tuple, rid)) {
        return false;
      }
      left_tuple_ = *tuple;
      is_left_tuple_over_ = false;
      // 每次更新left_tuple_，要记得更新is_match_
      is_match_ = false;
    }

    Tuple right_tuple{};

    while (right_executor_->Next(&right_tuple, rid)) {
      Value value = plan_->Predicate()->EvaluateJoin(&left_tuple_, left_executor_->GetOutputSchema(), &right_tuple,
                                                     right_executor_->GetOutputSchema());
      if (value.IsNull() || !value.GetAs<bool>()) {
        continue;
      }

      std::vector<Value> values;
      for (uint32_t i = 0; i < left_executor_->GetOutputSchema().GetColumnCount(); i++) {
        values.emplace_back(left_tuple_.GetValue(&(left_executor_->GetOutputSchema()), i));
      }
      for (uint32_t i = 0; i < right_executor_->GetOutputSchema().GetColumnCount(); i++) {
        values.emplace_back(right_tuple.GetValue(&(right_executor_->GetOutputSchema()), i));
      }

      *tuple = Tuple(values, &GetOutputSchema());
      is_match_ = true;
      return true;
    }
    // 表示当前处理的left_tuple_和右表所有tuple比较完毕，之后切换left_tuple_
    is_left_tuple_over_ = true;

    // 如果left_tuple没有匹配到任何right_tuple，且为left join，则直接根据left_tuple连接null生成结果tuple
    if (!is_match_ && plan_->GetJoinType() == JoinType::LEFT) {
      std::vector<Value> values;
      for (uint32_t i = 0; i < left_executor_->GetOutputSchema().GetColumnCount(); i++) {
        values.emplace_back(left_tuple_.GetValue(&(left_executor_->GetOutputSchema()), i));
      }
      for (uint32_t i = 0; i < right_executor_->GetOutputSchema().GetColumnCount(); i++) {
        values.emplace_back(
            ValueFactory::GetNullValueByType(right_executor_->GetOutputSchema().GetColumn(i).GetType()));
      }
      *tuple = Tuple(values, &GetOutputSchema());
      // 如果右子结点匹配结束，重置右子结点。总体的算法是naive nested loop join，性能很差
      right_executor_->Init();
      return true;
    }

    // 假如没有匹配成功且不为left join
    right_executor_->Init();
  }
}

}  // namespace bustub
