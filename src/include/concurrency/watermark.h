#pragma once

#include <functional>
#include <queue>
#include <unordered_map>
#include <vector>

#include "concurrency/transaction.h"
#include "storage/table/tuple.h"

namespace bustub {

/**
 * @brief tracks all the read timestamps.
 *
 */
class Watermark {
 public:
  explicit Watermark(timestamp_t commit_ts) : commit_ts_(commit_ts), watermark_(commit_ts) {}

  auto AddTxn(timestamp_t read_ts) -> void;

  auto RemoveTxn(timestamp_t read_ts) -> void;

  /** The caller should update commit ts before removing the txn from the watermark so that we can track watermark
   * correctly. */
  auto UpdateCommitTs(timestamp_t commit_ts) { commit_ts_ = commit_ts; }

  auto GetWatermark() -> timestamp_t {
    if (current_reads_.empty()) {
      return commit_ts_;
    }
    return watermark_;
  }

  timestamp_t commit_ts_;

  // 这个类的设计主要就是计算这个成员变量
  timestamp_t watermark_;

  std::unordered_map<timestamp_t, int> current_reads_;

  std::priority_queue<timestamp_t, std::vector<timestamp_t>, std::greater<>> min_heap_;
};

};  // namespace bustub
