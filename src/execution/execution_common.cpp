//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// execution_common.cpp
//
// Identification: src/execution/execution_common.cpp
//
// Copyright (c) 2024-2024, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "execution/execution_common.h"

#include "catalog/catalog.h"
#include "common/macros.h"
#include "concurrency/transaction_manager.h"
#include "fmt/core.h"
#include "storage/table/table_heap.h"

namespace bustub {

TupleComparator::TupleComparator(std::vector<OrderBy> order_bys) : order_bys_(std::move(order_bys)) {}

auto TupleComparator::operator()(const SortEntry &entry_a, const SortEntry &entry_b) const -> bool {
  for (size_t i = 0; i < entry_a.first.size(); i++) {
    if (order_bys_[i].first == OrderByType::INVALID) {
      return false;
    }

    if (order_bys_[i].first == OrderByType::DEFAULT || order_bys_[i].first == OrderByType::ASC) {
      if (entry_a.first[i].CompareLessThan(entry_b.first[i]) == CmpBool::CmpTrue) {
        return true;
      }
      if (entry_a.first[i].CompareGreaterThan(entry_b.first[i]) == CmpBool::CmpTrue) {
        return false;
      }
    }

    if (order_bys_[i].first == OrderByType::DESC) {
      if (entry_a.first[i].CompareGreaterThan(entry_b.first[i]) == CmpBool::CmpTrue) {
        return true;
      }
      if (entry_a.first[i].CompareLessThan(entry_b.first[i]) == CmpBool::CmpTrue) {
        return false;
      }
    }
  }
  // order by 中每一项全相等
  return true;
}

auto GenerateSortKey(const Tuple &tuple, const std::vector<OrderBy> &order_bys, const Schema &schema) -> SortKey {
  SortKey sort_key{};
  for (const auto &order_by : order_bys) {
    sort_key.emplace_back(order_by.second->Evaluate(&tuple, schema));
  }

  return sort_key;
}

/**
 * Above are all you need for P3.
 * You can ignore the remaining part of this file until P4.
 */

/**
 * @brief Reconstruct a tuple by applying the provided undo logs from the base tuple. All logs in the undo_logs are
 * applied regardless of the timestamp
 *
 * @param schema The schema of the base tuple and the returned tuple.
 * @param base_tuple The base tuple to start the reconstruction from.
 * @param base_meta The metadata of the base tuple.
 * @param undo_logs The list of undo logs to apply during the reconstruction, the front is applied first.
 * @return An optional tuple that represents the reconstructed tuple. If the tuple is deleted as the result, returns
 * std::nullopt.
 */
auto ReconstructTuple(const Schema *schema, const Tuple &base_tuple, const TupleMeta &base_meta,
                      const std::vector<UndoLog> &undo_logs) -> std::optional<Tuple> {
  // 特殊情况：base_tuple为deleted，且undo_logs中为空，则重构后依旧为deleted，则返回nullopt
  if (base_meta.is_deleted_ && undo_logs.empty()) {
    return std::nullopt;
  }

  // 成员为vector，默认拷贝构造即深拷贝
  Tuple tuple(base_tuple);
  bool delete_flag = false;
  for (const auto &undo_log : undo_logs) {
    if (undo_log.is_deleted_) {
      delete_flag = true;
      continue;
    }

    delete_flag = false;
    std::vector<Value> values;
    std::vector<Column> columns;
    // 先获取目前undo_log的需要修改的列组成的undolog_schema
    for (int i = 0; i < static_cast<int>(undo_log.modified_fields_.size()); i++) {
      if (undo_log.modified_fields_[i]) {
        columns.emplace_back(schema->GetColumn(i));
      }
    }
    Schema undo_log_schema(columns);

    int idx = 0;
    for (int i = 0; i < static_cast<int>(undo_log.modified_fields_.size()); i++) {
      if (undo_log.modified_fields_[i]) {
        values.emplace_back(undo_log.tuple_.GetValue(&undo_log_schema, idx++));
      } else {
        values.emplace_back(tuple.GetValue(schema, i));
      }
    }
    tuple = {values, schema};
  }

  if (delete_flag) {
    return std::nullopt;
  }
  return tuple;
}

/**
 * @brief Collects the undo logs sufficient to reconstruct the tuple w.r.t. the txn.
 *
 * @param rid The RID of the tuple.
 * @param base_meta The metadata of the base tuple.
 * @param base_tuple The base tuple.
 * @param undo_link The undo link to the latest undo log.
 * @param txn The transaction.
 * @param txn_mgr The transaction manager.
 * @return An optional vector of undo logs to pass to ReconstructTuple(). std::nullopt if the tuple did not exist at the
 * time.
 */
auto CollectUndoLogs(RID rid, const TupleMeta &base_meta, const Tuple &base_tuple, std::optional<UndoLink> undo_link,
                     Transaction *txn, TransactionManager *txn_mgr) -> std::optional<std::vector<UndoLog>> {
  // p.s. 此函数中不需要管 base_tuple 和 undo_log 的 delete 字段，只需要收集需要用到的UndoLog
  // 对于delete的处理在ReconstructTuple函数中进行

  // 1. 如果table heap中元组是相对事务read_ts_最新的数据
  // p.s. 被删除也是被修改，会修改ts_为tmp_ts，大小大于commit_ts范围，所以不会在此类情况中。所以不需要判断delete
  auto read_ts = txn->GetReadTs();
  if (base_meta.ts_ <= read_ts) {
    return std::vector<UndoLog>();
  }

  // 2. 如果table heap中元组被当前事务修改
  if (base_meta.ts_ >= TXN_START_ID && base_meta.ts_ == txn->GetTransactionTempTs()) {
    return std::vector<UndoLog>();
  }

  // 3. 如果table heap中元组被其他未提交事务修改 or table heap中元组比当前事务read_ts_更新
  if (!undo_link.has_value()) {
    return std::nullopt;
  }
  auto tmp_link = undo_link.value();
  std::vector<UndoLog> undo_logs;
  while (tmp_link.IsValid()) {
    auto undo_log = txn_mgr->GetUndoLog(tmp_link);
    timestamp_t ts = undo_log.ts_;
    undo_logs.emplace_back(undo_log);

    if (ts <= read_ts) {
      return undo_logs;
    }

    tmp_link = undo_log.prev_version_;
  }
  return std::nullopt;
}

/**
 * @brief Generates a new undo log as the transaction tries to modify this tuple at the first time.
 *
 * @param schema The schema of the table.
 * @param base_tuple The base tuple before the update, the one retrieved from the table heap. nullptr if the tuple is
 * deleted.
 * @param target_tuple The target tuple after the update. nullptr if this is a deletion.
 * @param ts The timestamp of the base tuple.
 * @param prev_version The undo link to the latest undo log of this tuple.
 * @return The generated undo log.
 */
auto GenerateNewUndoLog(const Schema *schema, const Tuple *base_tuple, const Tuple *target_tuple, timestamp_t ts,
                        UndoLink prev_version) -> UndoLog {
  UNIMPLEMENTED("not implemented");
}

/**
 * @brief Generate the updated undo log to replace the old one, whereas the tuple is already modified by this txn once.
 *
 * @param schema The schema of the table.
 * @param base_tuple The base tuple before the update, the one retrieved from the table heap. nullptr if the tuple is
 * deleted.
 * @param target_tuple The target tuple after the update. nullptr if this is a deletion.
 * @param log The original undo log.
 * @return The updated undo log.
 */
auto GenerateUpdatedUndoLog(const Schema *schema, const Tuple *base_tuple, const Tuple *target_tuple,
                            const UndoLog &log) -> UndoLog {
  UNIMPLEMENTED("not implemented");
}

void TxnMgrDbg(const std::string &info, TransactionManager *txn_mgr, const TableInfo *table_info,
               TableHeap *table_heap) {
  // always use stderr for printing logs...
  fmt::println(stderr, "debug_hook: {}", info);

  // fmt::println(
  //     stderr,
  //     "You see this line of text because you have not implemented `TxnMgrDbg`. You should do this once you have "
  //     "finished task 2. Implementing this helper function will save you a lot of time for debugging in later
  //     tasks.");
  auto iter = table_heap->MakeIterator();
  while (!iter.IsEnd()) {
    RID rid = iter.GetRID();
    auto tuple_info = iter.GetTuple();
    fmt::println("RID={}/{} ts={} {} tuple={} ", std::to_string(rid.GetPageId()), std::to_string(rid.GetSlotNum()),
                 std::to_string(tuple_info.first.ts_), tuple_info.first.is_deleted_ ? "<del marker>" : "",
                 tuple_info.second.ToString(&table_info->schema_));
    auto undo_link = txn_mgr->GetUndoLink(rid);
    std::vector<Value> vec;
    for (size_t i = 0; i < table_info->schema_.GetColumnCount(); i++) {
      vec.push_back(tuple_info.second.GetValue(&table_info->schema_, i));
    }
    if (undo_link.has_value()) {
      if (txn_mgr->txn_map_.count(undo_link->prev_txn_) == 0) {
        ++iter;
        continue;
      }
      auto log = txn_mgr->GetUndoLog(undo_link.value());
      std::vector<Column> cols;
      for (size_t i = 0; i < log.modified_fields_.size(); i++) {
        if (log.modified_fields_[i]) {
          cols.push_back(table_info->schema_.GetColumn(i));
          // vec[i] = log.tuple_.GetValue(log.tuple_, i);
        }
      }
      auto log_schema = Schema(cols);
      size_t col_idx = 0;
      for (size_t i = 0; i < log.modified_fields_.size(); i++) {
        if (log.modified_fields_[i]) {
          // cols.push_back(schema->GetColumn(i));
          vec[i] = log.tuple_.GetValue(&log_schema, col_idx++);
        }
      }
      fmt::println("   txn{}@{} {} tuple={} ts={}", std::to_string(undo_link->prev_txn_ - TXN_START_ID),
                   std::to_string(undo_link->prev_log_idx_), log.is_deleted_ ? "<del marker>" : "",
                   Tuple(vec, &table_info->schema_).ToString(&table_info->schema_), std::to_string(log.ts_));
      while (log.prev_version_.IsValid()) {
        if (txn_mgr->txn_map_.count(log.prev_version_.prev_txn_) == 0) {
          break;
        }
        undo_link = log.prev_version_;
        log = txn_mgr->GetUndoLog(log.prev_version_);
        std::vector<Column> cols;
        for (size_t i = 0; i < log.modified_fields_.size(); i++) {
          if (log.modified_fields_[i]) {
            cols.push_back(table_info->schema_.GetColumn(i));
            // vec[i] = log.tuple_.GetValue(log.tuple_, i);
          }
        }
        auto log_schema = Schema(cols);
        size_t col_idx = 0;
        for (size_t i = 0; i < log.modified_fields_.size(); i++) {
          if (log.modified_fields_[i]) {
            // cols.push_back(schema->GetColumn(i));
            vec[i] = log.tuple_.GetValue(&log_schema, col_idx++);
          }
        }
        fmt::println("   txn{}@{} {} tuple={} ts={}", std::to_string(undo_link->prev_txn_ - TXN_START_ID),
                     std::to_string(undo_link->prev_log_idx_), log.is_deleted_ ? "<del marker>" : "",
                     Tuple(vec, &table_info->schema_).ToString(&table_info->schema_), std::to_string(log.ts_));
      }
    }
    ++iter;
  }
  fmt::println("");

  // We recommend implementing this function as traversing the table heap and print the version chain. An example output
  // of our reference solution:
  //
  // debug_hook: before verify scan
  // RID=0/0 ts=txn8 tuple=(1, <NULL>, <NULL>)
  //   txn8@0 (2, _, _) ts=1
  // RID=0/1 ts=3 tuple=(3, <NULL>, <NULL>)
  //   txn5@0 <del> ts=2
  //   txn3@0 (4, <NULL>, <NULL>) ts=1
  // RID=0/2 ts=4 <del marker> tuple=(<NULL>, <NULL>, <NULL>)
  //   txn7@0 (5, <NULL>, <NULL>) ts=3
  // RID=0/3 ts=txn6 <del marker> tuple=(<NULL>, <NULL>, <NULL>)
  //   txn6@0 (6, <NULL>, <NULL>) ts=2
  //   txn3@1 (7, _, _) ts=1
}

}  // namespace bustub
