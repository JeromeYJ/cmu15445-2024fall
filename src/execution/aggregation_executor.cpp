//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// aggregation_executor.cpp
//
// Identification: src/execution/aggregation_executor.cpp
//
// Copyright (c) 2015-2021, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//
#include <memory>
#include <vector>

#include "execution/executors/aggregation_executor.h"

namespace bustub {

AggregationExecutor::AggregationExecutor(ExecutorContext *exec_ctx, const AggregationPlanNode *plan,
                                         std::unique_ptr<AbstractExecutor> &&child_executor)
    : AbstractExecutor(exec_ctx),
      plan_(plan),
      child_executor_(std::move(child_executor)),
      aht_(plan_->aggregates_, plan_->agg_types_),
      aht_iterator_(aht_.Begin()) {}

void AggregationExecutor::Init() {
  child_executor_->Init();
  // 有些情况下，会对该算子多次Init，每次Init需要清空hashtable
  // nested loop join中测出的bug
  aht_.Clear();

  // In the context of a query plan, aggregations are pipeline breakers.
  // This may influence the way that you use the AggregationExecutor::Init() and AggregationExecutor::Next() functions
  // in your implementation. Carefully decide whether the build phase of the aggregation should be performed in
  // AggregationExecutor::Init() or AggregationExecutor::Next().
  Tuple tuple{};
  RID rid{};

  while (child_executor_->Next(&tuple, &rid)) {
    aht_.InsertCombine(MakeAggregateKey(&tuple), MakeAggregateValue(&tuple));
  }
  aht_iterator_ = aht_.Begin();

  // 如果此时hashtable为空
  if (aht_iterator_ == aht_.End() && plan_->GetGroupBys().empty()) {
    is_empty_ = true;
  }
}

auto AggregationExecutor::Next(Tuple *tuple, RID *rid) -> bool {
  // 会出现子算子查询结果为空，无tuple返回，此时hashtable为空，可能有COUNT(*)，所以需要单独处理
  if (is_empty_) {
    Schema schema = bustub::AggregationPlanNode::InferAggSchema(plan_->GetGroupBys(), plan_->GetAggregates(),
                                                                plan_->GetAggregateTypes());
    *tuple = Tuple{aht_.GenerateInitialAggregateValue().aggregates_, &schema};
    is_empty_ = false;
    return true;
  }

  if (aht_iterator_ == aht_.End()) {
    return false;
  }

  std::vector<Value> values;
  for (const auto &value : aht_iterator_.Key().group_bys_) {
    values.push_back(value);
  }
  for (const auto &value : aht_iterator_.Val().aggregates_) {
    values.push_back(value);
  }
  Schema schema = bustub::AggregationPlanNode::InferAggSchema(plan_->GetGroupBys(), plan_->GetAggregates(),
                                                              plan_->GetAggregateTypes());
  *tuple = Tuple{values, &schema};

  ++aht_iterator_;
  return true;
}

auto AggregationExecutor::GetChildExecutor() const -> const AbstractExecutor * { return child_executor_.get(); }

}  // namespace bustub
