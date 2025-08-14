#include "concurrency/watermark.h"
#include <exception>
#include "common/exception.h"

namespace bustub {

auto Watermark::AddTxn(timestamp_t read_ts) -> void {
  if (read_ts < commit_ts_) {
    throw Exception("read ts < commit ts");
  }

  // TODO(fall2023): implement me!
  current_reads_[read_ts]++;
  min_heap_.push(read_ts);
  // 这里可以先判断是否当前min_heap_中只有刚刚插入的新元素，如果是，表示目前watermark_为commit_ts_，则可以直接更新watermark_
  if (min_heap_.size() == 1 || watermark_ > read_ts) {
    watermark_ = read_ts;
  }
}

auto Watermark::RemoveTxn(timestamp_t read_ts) -> void {
  // TODO(fall2023): implement me!
  if (current_reads_.count(read_ts) == 0) {
    return;
  }
  if (current_reads_[read_ts] > 0) {
    current_reads_[read_ts]--;
  }

  // 如果删掉的是watermark，则将优先队列更新，将其中的部分无效元素删除
  // 采用惰性删除策略
  if (read_ts == watermark_) {
    while (!min_heap_.empty()) {
      auto top_ts = min_heap_.top();
      // 找到了有效堆顶元素，则更新watermark_并直接返回
      if (current_reads_[top_ts] > 0) {
        watermark_ = top_ts;
        return;
      }
      min_heap_.pop();
    }

    // 如果最后min_heap_为空，则表示没有有效read_ts了，则使用commit_ts作为watermark_
    watermark_ = commit_ts_;
  }
}

}  // namespace bustub
