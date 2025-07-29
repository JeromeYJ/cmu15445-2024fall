#include <algorithm>
#include <memory>
#include "catalog/column.h"
#include "catalog/schema.h"
#include "common/exception.h"
#include "common/macros.h"
#include "execution/expressions/column_value_expression.h"
#include "execution/expressions/comparison_expression.h"
#include "execution/expressions/constant_value_expression.h"
#include "execution/expressions/logic_expression.h"
#include "execution/plans/abstract_plan.h"
#include "execution/plans/filter_plan.h"
#include "execution/plans/hash_join_plan.h"
#include "execution/plans/nested_loop_join_plan.h"
#include "execution/plans/projection_plan.h"
#include "optimizer/optimizer.h"
#include "type/type_id.h"

namespace bustub {

// 递归处理表达式树的函数
auto Helper(std::vector<AbstractExpressionRef> &left_key_expressions,
            std::vector<AbstractExpressionRef> &right_key_expressions, const AbstractExpressionRef &predicate) -> bool {
  const auto logic_expr = dynamic_cast<const LogicExpression *>(predicate.get());
  if (logic_expr == nullptr) {
    const auto comparison_expr = dynamic_cast<const ComparisonExpression *>(predicate.get());
    if (comparison_expr == nullptr || comparison_expr->comp_type_ != ComparisonType::Equal) {
      return false;
    }

    const auto col_value_expr_1 = dynamic_cast<const ColumnValueExpression *>(comparison_expr->GetChildAt(0).get());
    if (col_value_expr_1->GetTupleIdx() == 0) {
      left_key_expressions.emplace_back(comparison_expr->GetChildAt(0));
      right_key_expressions.emplace_back(comparison_expr->GetChildAt(1));
    } else {
      right_key_expressions.emplace_back(comparison_expr->GetChildAt(0));
      left_key_expressions.emplace_back(comparison_expr->GetChildAt(1));
    }
    return true;
  }

  if (logic_expr->logic_type_ != LogicType::And) {
    return false;
  }

  return Helper(left_key_expressions, right_key_expressions, predicate->GetChildAt(0)) &&
         Helper(left_key_expressions, right_key_expressions, predicate->GetChildAt(1));
}

auto Optimizer::OptimizeNLJAsHashJoin(const AbstractPlanNodeRef &plan) -> AbstractPlanNodeRef {
  // TODO(student): implement NestedLoopJoin -> HashJoin optimizer rule
  // Note for 2023 Fall: You should support join keys of any number of conjunction of equi-conditions:
  // E.g. <column expr> = <column expr> AND <column expr> = <column expr> AND ...
  std::vector<AbstractPlanNodeRef> children;
  for (const auto &child : plan->GetChildren()) {
    children.emplace_back(OptimizeNLJAsHashJoin(child));
  }
  auto optimized_plan = plan->CloneWithChildren(std::move(children));

  if (optimized_plan->GetType() == PlanType::NestedLoopJoin) {
    const auto &nlj_plan = dynamic_cast<const NestedLoopJoinPlanNode &>(*optimized_plan);
    // Has exactly two children
    BUSTUB_ENSURE(nlj_plan.children_.size() == 2, "NLJ should have exactly 2 children.");

    // 检查是否为filter表达式是否为多个通过 AND 连接的 equi-condition 组成
    std::vector<AbstractExpressionRef> left_key_expressions;
    std::vector<AbstractExpressionRef> right_key_expressions;
    if (!Helper(left_key_expressions, right_key_expressions, nlj_plan.Predicate())) {
      return optimized_plan;
    }
    return std::make_shared<HashJoinPlanNode>(optimized_plan->output_schema_, optimized_plan->GetChildAt(0),
                                              optimized_plan->GetChildAt(1), left_key_expressions,
                                              right_key_expressions, nlj_plan.GetJoinType());
  }

  return optimized_plan;
}

}  // namespace bustub
