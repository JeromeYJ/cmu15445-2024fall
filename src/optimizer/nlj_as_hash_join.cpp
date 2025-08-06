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

// leaderboard-q1
auto HelperQ1(std::vector<AbstractExpressionRef> &left_key_expressions,
              std::vector<AbstractExpressionRef> &right_key_expressions, AbstractExpressionRef &left_predicate,
              AbstractExpressionRef &right_predicate, const AbstractExpressionRef &predicate, const Schema &schema,
              const Schema &child_left_schema, const Schema &child_right_schema) -> bool {
  const auto logic_expr = dynamic_cast<const LogicExpression *>(predicate.get());
  if (logic_expr == nullptr) {
    const auto comparison_expr = dynamic_cast<const ComparisonExpression *>(predicate.get());
    if (comparison_expr == nullptr) {
      return false;
    }

    const auto col_value_expr_1 = dynamic_cast<const ColumnValueExpression *>(comparison_expr->GetChildAt(0).get());
    const auto col_value_expr_2 = dynamic_cast<const ColumnValueExpression *>(comparison_expr->GetChildAt(1).get());

    if (comparison_expr->comp_type_ != ComparisonType::Equal && col_value_expr_1 == nullptr &&
        col_value_expr_2 == nullptr) {
      return false;
    }

    if (col_value_expr_2 == nullptr) {
      if (col_value_expr_1->GetTupleIdx() == 0) {
        std::string col_name = schema.GetColumn(col_value_expr_1->GetColIdx()).GetName();
        auto col_idx = child_left_schema.TryGetColIdx(col_name);
        if (col_idx) {
          auto col_type = child_left_schema.GetColumn(*col_idx);
          auto expr = ComparisonExpression(std::make_shared<ColumnValueExpression>(0, *col_idx, col_type),
                                           comparison_expr->GetChildAt(1), comparison_expr->comp_type_);
          if (left_predicate == nullptr) {
            left_predicate = std::make_shared<ComparisonExpression>(expr);
          } else {
            left_predicate = std::make_shared<LogicExpression>(std::make_shared<ComparisonExpression>(expr),
                                                               left_predicate, LogicType::And);
          }
        } else {
          col_idx = child_right_schema.TryGetColIdx(col_name);
          auto col_type = child_right_schema.GetColumn(*col_idx);
          auto expr = ComparisonExpression(std::make_shared<ColumnValueExpression>(1, *col_idx, col_type),
                                           comparison_expr->GetChildAt(1), comparison_expr->comp_type_);
          if (left_predicate == nullptr) {
            left_predicate = std::make_shared<ComparisonExpression>(expr);
          } else {
            left_predicate = std::make_shared<LogicExpression>(std::make_shared<ComparisonExpression>(expr),
                                                               left_predicate, LogicType::And);
          }
        }
      } else {
        auto expr = ComparisonExpression(std::make_shared<ColumnValueExpression>(0, col_value_expr_1->GetColIdx(),
                                                                                 col_value_expr_1->GetReturnType()),
                                         comparison_expr->GetChildAt(1), comparison_expr->comp_type_);
        if (right_predicate == nullptr) {
          right_predicate = std::make_shared<ComparisonExpression>(expr);
        } else {
          right_predicate = std::make_shared<LogicExpression>(std::make_shared<ComparisonExpression>(expr),
                                                              right_predicate, LogicType::And);
        }
      }
      return true;
    }

    if (col_value_expr_1->GetTupleIdx() == 0 && col_value_expr_2->GetTupleIdx() == 0) {
      std::string col_name_1 = schema.GetColumn(col_value_expr_1->GetColIdx()).GetName();
      auto col_idx_1 = child_left_schema.TryGetColIdx(col_name_1);
      std::string col_name_2 = schema.GetColumn(col_value_expr_2->GetColIdx()).GetName();
      auto col_idx_2 = child_left_schema.TryGetColIdx(col_name_2);
      std::shared_ptr<ColumnValueExpression> col_value_expr_1 = nullptr;
      std::shared_ptr<ColumnValueExpression> col_value_expr_2 = nullptr;
      if (col_idx_1) {
        auto col_type = child_left_schema.GetColumn(*col_idx_1);
        col_value_expr_1 = std::make_shared<ColumnValueExpression>(0, *col_idx_1, col_type);
      } else {
        col_idx_1 = child_right_schema.TryGetColIdx(col_name_1);
        auto col_type = child_right_schema.GetColumn(*col_idx_1);
        col_value_expr_1 = std::make_shared<ColumnValueExpression>(1, *col_idx_1, col_type);
      }

      if (col_idx_2) {
        auto col_type = child_left_schema.GetColumn(*col_idx_2);
        col_value_expr_2 = std::make_shared<ColumnValueExpression>(0, *col_idx_2, col_type);
      } else {
        col_idx_2 = child_right_schema.TryGetColIdx(col_name_2);
        auto col_type = child_right_schema.GetColumn(*col_idx_2);
        col_value_expr_2 = std::make_shared<ColumnValueExpression>(1, *col_idx_2, col_type);
      }

      auto expr = ComparisonExpression(col_value_expr_1, col_value_expr_2, comparison_expr->comp_type_);
      if (left_predicate == nullptr) {
        left_predicate = std::make_shared<ComparisonExpression>(expr);
      } else {
        left_predicate = std::make_shared<LogicExpression>(std::make_shared<ComparisonExpression>(expr), left_predicate,
                                                           LogicType::And);
      }
    } else if (col_value_expr_1->GetTupleIdx() == 0) {
      left_key_expressions.emplace_back(comparison_expr->GetChildAt(0));
      right_key_expressions.emplace_back(comparison_expr->GetChildAt(1));
    } else if (col_value_expr_2->GetTupleIdx() == 0) {
      right_key_expressions.emplace_back(comparison_expr->GetChildAt(0));
      left_key_expressions.emplace_back(comparison_expr->GetChildAt(1));
    } else {
      if (right_predicate == nullptr) {
        right_predicate = std::make_shared<ComparisonExpression>(*comparison_expr);
      } else {
        right_predicate = std::make_shared<LogicExpression>(std::make_shared<ComparisonExpression>(*comparison_expr),
                                                            right_predicate, LogicType::And);
      }
    }
    return true;
  }

  if (logic_expr->logic_type_ != LogicType::And) {
    return false;
  }

  return HelperQ1(left_key_expressions, right_key_expressions, left_predicate, right_predicate,
                  predicate->GetChildAt(0), schema, child_left_schema, child_right_schema) &&
         HelperQ1(left_key_expressions, right_key_expressions, left_predicate, right_predicate,
                  predicate->GetChildAt(1), schema, child_left_schema, child_right_schema);
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
    if (Helper(left_key_expressions, right_key_expressions, nlj_plan.Predicate())) {
      return std::make_shared<HashJoinPlanNode>(optimized_plan->output_schema_, optimized_plan->GetChildAt(0),
                                                optimized_plan->GetChildAt(1), left_key_expressions,
                                                right_key_expressions, nlj_plan.GetJoinType());
    }

    // left_key_expressions.clear();
    // right_key_expressions.clear();
    // AbstractExpressionRef left_predicate = nullptr;
    // AbstractExpressionRef right_predicate = nullptr;
    // auto left_child_plan = optimized_plan->GetChildAt(0);
    // auto right_child_plan = optimized_plan->GetChildAt(1);
    // if (left_child_plan->GetChildren().empty()) {
    //   return optimized_plan;
    // }
    // Schema child_left_schema = left_child_plan->GetChildAt(0)->OutputSchema();
    // Schema child_right_schema = left_child_plan->GetChildAt(1)->OutputSchema();
    // if (right_child_plan->GetType() == PlanType::NestedLoopJoin) {
    //   child_left_schema = left_child_plan->GetChildAt(0)->OutputSchema();
    //   child_right_schema = right_child_plan->GetChildAt(1)->OutputSchema();
    // }
    // if (HelperQ1(left_key_expressions, right_key_expressions, left_predicate, right_predicate, nlj_plan.Predicate(),
    // optimized_plan->OutputSchema(), child_left_schema, child_right_schema)) {
    //   AbstractPlanNodeRef left = std::make_shared<FilterPlanNode>(left_child_plan->output_schema_, left_predicate,
    //   left_child_plan); AbstractPlanNodeRef right =
    //   std::make_shared<FilterPlanNode>(right_child_plan->output_schema_, right_predicate, right_child_plan); if
    //   (left_child_plan->GetType() == PlanType::NestedLoopJoin) {
    //     const auto& child_nlj = dynamic_cast<const NestedLoopJoinPlanNode &>(*left_child_plan);
    //     left = std::make_shared<NestedLoopJoinPlanNode>(left_child_plan->output_schema_,
    //     left_child_plan->GetChildAt(0), left_child_plan->GetChildAt(1), left_predicate, child_nlj.GetJoinType());
    //   }
    //   if (right_child_plan->GetType() == PlanType::NestedLoopJoin) {
    //     const auto& child_nlj = dynamic_cast<const NestedLoopJoinPlanNode &>(*right_child_plan);
    //     right = std::make_shared<NestedLoopJoinPlanNode>(right_child_plan->output_schema_,
    //     right_child_plan->GetChildAt(0), right_child_plan->GetChildAt(1), right_predicate, child_nlj.GetJoinType());
    //   }
    //   return std::make_shared<HashJoinPlanNode>(optimized_plan->output_schema_, left,
    //                                           right, left_key_expressions,
    //                                           right_key_expressions, nlj_plan.GetJoinType());
    // }

    return optimized_plan;
  }

  return optimized_plan;
}

}  // namespace bustub
