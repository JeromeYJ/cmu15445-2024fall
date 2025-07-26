//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// nested_index_join_executor.cpp
//
// Identification: src/execution/nested_index_join_executor.cpp
//
// Copyright (c) 2015-19, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "execution/executors/nested_index_join_executor.h"

namespace bustub {

NestIndexJoinExecutor::NestIndexJoinExecutor(ExecutorContext *exec_ctx, const NestedIndexJoinPlanNode *plan,
                                             std::unique_ptr<AbstractExecutor> &&child_executor)
    : AbstractExecutor(exec_ctx),
      plan_(plan),
      child_executor_(std::move(child_executor)),
      index_info_(exec_ctx_->GetCatalog()->GetIndex(plan_->GetIndexOid())),
      table_info_(exec_ctx_->GetCatalog()->GetTable(plan_->GetInnerTableOid())) {
  if (!(plan->GetJoinType() == JoinType::LEFT || plan->GetJoinType() == JoinType::INNER)) {
    // Note for 2023 Spring: You ONLY need to implement left join and inner join.
    throw bustub::NotImplementedException(fmt::format("join type {} not supported", plan->GetJoinType()));
  }
}

void NestIndexJoinExecutor::Init() { child_executor_->Init(); }

auto NestIndexJoinExecutor::Next(Tuple *tuple, RID *rid) -> bool {
  Tuple child_tuple{};

  while (child_executor_->Next(&child_tuple, rid)) {
    std::vector<RID> result;
    Value value = plan_->KeyPredicate()->Evaluate(&child_tuple, child_executor_->GetOutputSchema());
    Tuple key_tuple({value}, &(index_info_->key_schema_));
    index_info_->index_->ScanKey(key_tuple, &result, exec_ctx_->GetTransaction());

    if (result.empty()) {
      if (plan_->GetJoinType() == JoinType::LEFT) {
        std::vector<Value> values;
        for (uint32_t i = 0; i < child_executor_->GetOutputSchema().GetColumnCount(); i++) {
          values.emplace_back(child_tuple.GetValue(&(child_executor_->GetOutputSchema()), i));
        }
        for (uint32_t i = 0; i < plan_->InnerTableSchema().GetColumnCount(); i++) {
          values.emplace_back(ValueFactory::GetNullValueByType(plan_->InnerTableSchema().GetColumn(i).GetType()));
        }

        *tuple = Tuple(values, &GetOutputSchema());
        return true;
      }

      // 如果不为left join且索引无结果，循环处理下一child tuple
      continue;
    }

    // result不为空，则返回结果tuple
    Tuple right_tuple = table_info_->table_->GetTuple(result[0]).second;
    std::vector<Value> values;
    for (uint32_t i = 0; i < child_executor_->GetOutputSchema().GetColumnCount(); i++) {
      values.emplace_back(child_tuple.GetValue(&(child_executor_->GetOutputSchema()), i));
    }
    for (uint32_t i = 0; i < plan_->InnerTableSchema().GetColumnCount(); i++) {
      values.emplace_back(right_tuple.GetValue(&(plan_->InnerTableSchema()), i));
    }

    *tuple = Tuple(values, &GetOutputSchema());
    return true;
  }

  return false;
}

}  // namespace bustub
