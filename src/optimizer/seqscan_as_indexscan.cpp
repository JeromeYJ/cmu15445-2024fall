#include "execution/expressions/column_value_expression.h"
#include "execution/expressions/comparison_expression.h"
#include "execution/expressions/constant_value_expression.h"
#include "execution/expressions/logic_expression.h"
#include "execution/plans/index_scan_plan.h"
#include "execution/plans/seq_scan_plan.h"
#include "optimizer/optimizer.h"

namespace bustub {

/**
 *  递归解析logic_expression的函数
 */
auto Helper(std::vector<AbstractExpressionRef> &pred_keys, std::vector<uint32_t> &filter_column_ids,
            const AbstractExpressionRef &expr) -> bool {
  const auto logic_expr = dynamic_cast<const LogicExpression *>(expr.get());

  // 若logic_expr已经不可继续解析，即已经为比较表达式，则进行比较表达式的处理
  if (logic_expr == nullptr) {
    // 当expression不为逻辑表达式，则判断是否为单个的比较表达式，且需要为等式比较表达式
    const auto comparison_expr = dynamic_cast<const ComparisonExpression *>(expr.get());
    if (comparison_expr == nullptr || comparison_expr->comp_type_ != ComparisonType::Equal) {
      // false表示不应该进行index_scan转换，不符合条件
      return false;
    }

    auto column_value_expr = dynamic_cast<ColumnValueExpression *>(comparison_expr->GetChildAt(0).get());
    // ConstantValueExpression *const_value_expr;
    if (column_value_expr == nullptr) {
      // const_value_expr = dynamic_cast<ConstantValueExpression *>(comparison_expr->GetChildAt(0).get());
      column_value_expr = dynamic_cast<ColumnValueExpression *>(comparison_expr->GetChildAt(1).get());
      pred_keys.emplace_back(comparison_expr->GetChildAt(0));
    } else {
      // const_value_expr = dynamic_cast<ConstantValueExpression *>(comparison_expr->GetChildAt(1).get());
      pred_keys.emplace_back(comparison_expr->GetChildAt(1));
    }

    // 确认filter中列是否均为同一列/同一组列，若不为同一列/同一组列则直接返回false。均为同一列/同一组列则将其存入column_value_expr中，column_value_expr中保持只有一个元素
    if (filter_column_ids.empty()) {
      filter_column_ids.emplace_back(column_value_expr->GetColIdx());
    } else {
      if (column_value_expr->GetColIdx() != filter_column_ids[0]) {
        return false;
      }
    }

    return true;
  }

  // 若logic_expr依旧可以解析，则继续解析
  // 若不为or语句，没有必要转换为index scan，直接返回false
  if (logic_expr->logic_type_ != LogicType::Or) {
    return false;
  }

  return Helper(pred_keys, filter_column_ids, logic_expr->GetChildAt(0)) &&
         Helper(pred_keys, filter_column_ids, logic_expr->GetChildAt(1));
}

auto Optimizer::OptimizeSeqScanAsIndexScan(const bustub::AbstractPlanNodeRef &plan) -> AbstractPlanNodeRef {
  // TODO(student): implement seq scan with predicate -> index scan optimizer rule
  // The Filter Predicate Pushdown has been enabled for you in optimizer.cpp when forcing starter rule
  std::vector<AbstractPlanNodeRef> children;
  for (const auto &child : plan->GetChildren()) {
    // 树结构，递归进行优化
    children.emplace_back(OptimizeSeqScanAsIndexScan(child));
  }
  auto optimized_plan = plan->CloneWithChildren(std::move(children));

  if (optimized_plan->GetType() == PlanType::SeqScan) {
    const auto &seq_scan = dynamic_cast<const SeqScanPlanNode &>(*optimized_plan);
    if (!seq_scan.filter_predicate_) {
      return optimized_plan;
    }

    std::vector<AbstractExpressionRef> pred_keys;
    std::vector<uint32_t> filter_column_ids;
    // 这里利用dynamic_cast在对象不是目标类型的转换中失败时返回nullptr的机制，进行expression类型的检查
    // 常见的多态机制

    if (!Helper(pred_keys, filter_column_ids, seq_scan.filter_predicate_)) {
      // 函数返回false表示不应该进行index_scan转换，不符合条件
      return optimized_plan;
    }

    const auto table_info = catalog_.GetTable(seq_scan.GetTableOid());
    const auto indices = catalog_.GetTableIndexes(table_info->name_);

    for (const auto &index : indices) {
      const auto &columns = index->index_->GetKeyAttrs();
      if (filter_column_ids == columns) {
        return std::make_shared<IndexScanPlanNode>(optimized_plan->output_schema_, seq_scan.GetTableOid(),
                                                   index->index_oid_, seq_scan.filter_predicate_, pred_keys);
      }
    }
  }

  return optimized_plan;
}

}  // namespace bustub
