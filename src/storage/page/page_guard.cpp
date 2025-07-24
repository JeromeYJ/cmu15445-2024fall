//===----------------------------------------------------------------------===//
//
//                         BusTub
//
// page_guard.cpp
//
// Identification: src/storage/page/page_guard.cpp
//
// Copyright (c) 2024-2024, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "storage/page/page_guard.h"

namespace bustub {

/**
 * @brief The only constructor for an RAII `ReadPageGuard` that creates a valid guard.
 *
 * Note that only the buffer pool manager is allowed to call this constructor.
 *
 * TODO(P1): Add implementation.
 *
 * @param page_id The page ID of the page we want to read.
 * @param frame A shared pointer to the frame that holds the page we want to protect.
 * @param replacer A shared pointer to the buffer pool manager's replacer.
 * @param bpm_latch A shared pointer to the buffer pool manager's latch.
 *
 * 这里的shared_ptr指向对象的引用计数要结合实参、形参好好思考一下，外界调用这个函数shared_ptr类参数不要用move，直接拷贝
 * 不然buffer pool manager对象中的replacer_、bpm_latch_会失去资源所有权
 *
 * 这里要注意：构造函数列表初始化的顺序由声明顺序决定，成员变量按照它们在类定义中出现的顺序依次初始化。
 * 与初始化列表顺序无关！即使在构造函数初始化列表中调换了顺序，实际初始化仍按声明顺序进行。
 * 这里的lk初始化需要注意，不要用形参frame访问rwlock，不然会出现用空指针访问rwlock的情况
 * 而应该使用已经初始化的frame_
 */
ReadPageGuard::ReadPageGuard(page_id_t page_id, std::shared_ptr<FrameHeader> frame,
                             std::shared_ptr<LRUKReplacer> replacer, std::shared_ptr<std::mutex> bpm_latch)
    : page_id_(page_id),
      frame_(std::move(frame)),
      replacer_(std::move(replacer)),
      bpm_latch_(std::move(bpm_latch)),
      is_valid_(true) {
  bpm_latch_->unlock();
  // std::cout << "Reader"
  //         << " try to lock " << page_id << std::endl;
  frame_->rwlatch_.lock_shared();
  // std::cout << "Reader"
  //         << " locked " << page_id << std::endl;
  // frame_->pin_count_++;
  // frame_->page_id_ = page_id;
  // 记得更新 replacer_ 中 LRU-K 历史信息
  // 这个更新的位置要在这里，在外面会出现还没有访问到该页面，就修改了replacer_中LRU-K队列的情况
  // 不过好像放外面也不影响测试
  // replacer_->RecordAccess(frame_->frame_id_);
  // replacer_->SetEvictable(frame_->frame_id_, false);
}

/**
 * @brief The move constructor for `ReadPageGuard`.
 *
 * ### Implementation
 *
 * If you are unfamiliar with move semantics, please familiarize yourself with learning materials online. There are many
 * great resources (including articles, Microsoft tutorials, YouTube videos) that explain this in depth.
 *
 * Make sure you invalidate the other guard, otherwise you might run into double free problems! For both objects, you
 * need to update _at least_ 5 fields each.
 *
 * TODO(P1): Add implementation.
 *
 * @param that The other page guard.
 *
 * 可以在拷贝/移动构造函数中访问同类对象私有成员
 * 该机制方便拷贝/移动操作
 */
ReadPageGuard::ReadPageGuard(ReadPageGuard &&that) noexcept {
  if (this == &that) {
    return;
  }
  Drop();  // 这一行容易漏，是在vector的erase函数test中发现的问题，很难找到
  page_id_ = that.page_id_;
  frame_ = std::move(that.frame_);
  replacer_ = std::move(that.replacer_);
  bpm_latch_ = std::move(that.bpm_latch_);
  is_valid_ = that.is_valid_;
  that.is_valid_ = false;
}

/**
 * @brief The move assignment operator for `ReadPageGuard`.
 *
 * ### Implementation
 *
 * If you are unfamiliar with move semantics, please familiarize yourself with learning materials online. There are many
 * great resources (including articles, Microsoft tutorials, YouTube videos) that explain this in depth.
 *
 * Make sure you invalidate the other guard, otherwise you might run into double free problems! For both objects, you
 * need to update _at least_ 5 fields each, and for the current object, make sure you release any resources it might be
 * holding on to.
 *
 * TODO(P1): Add implementation.
 *
 * @param that The other page guard.
 * @return ReadPageGuard& The newly valid `ReadPageGuard`.
 */
auto ReadPageGuard::operator=(ReadPageGuard &&that) noexcept -> ReadPageGuard & {
  // 单纯因为测试用例过不了加的，个人觉得test case设计不太好
  // 很难出现要用自己移动构造自己的情况
  if (this == &that) {
    return *this;
  }
  Drop();  // 这一行容易漏，是在vector的erase函数test中发现的问题，很难找到。主要是Drop函数中会更新pin_count等信息
  page_id_ = that.page_id_;
  frame_ = std::move(that.frame_);
  replacer_ = std::move(that.replacer_);
  bpm_latch_ = std::move(that.bpm_latch_);
  is_valid_ = that.is_valid_;
  that.is_valid_ = false;
  return *this;
}

/**
 * @brief Gets the page ID of the page this guard is protecting.
 */
auto ReadPageGuard::GetPageId() const -> page_id_t {
  BUSTUB_ENSURE(is_valid_, "tried to use an invalid read guard");
  return page_id_;
}

/**
 * @brief Gets a `const` pointer to the page of data this guard is protecting.
 */
auto ReadPageGuard::GetData() const -> const char * {
  BUSTUB_ENSURE(is_valid_, "tried to use an invalid read guard");
  return frame_->GetData();
}

/**
 * @brief Returns whether the page is dirty (modified but not flushed to the disk).
 */
auto ReadPageGuard::IsDirty() const -> bool {
  BUSTUB_ENSURE(is_valid_, "tried to use an invalid read guard");
  return frame_->is_dirty_;
}

/**
 * @brief Manually drops a valid `ReadPageGuard`'s data. If this guard is invalid, this function does nothing.
 *
 * ### Implementation
 *
 * Make sure you don't double free! Also, think **very** **VERY** carefully about what resources you own and the order
 * in which you release those resources. If you get the ordering wrong, you will very likely fail one of the later
 * Gradescope tests. You may also want to take the buffer pool manager's latch in a very specific scenario...
 *
 * TODO(P1): Add implementation.
 */
void ReadPageGuard::Drop() {
  if (!is_valid_) {
    return;
  }

  // 这里要记得对buffer pool manager中的数据结构进行多线程保护！
  bpm_latch_->lock();
  frame_->pin_count_--;
  if (frame_->pin_count_.load() == 0) {
    replacer_->SetEvictable(frame_->frame_id_, true);
  }
  bpm_latch_->unlock();

  frame_->rwlatch_.unlock_shared();
  // std::cout << "Reader"
  //         << " unlocked " << page_id_ << std::endl;
  is_valid_ = false;
  replacer_ = nullptr;
  frame_ = nullptr;
}

/** @brief The destructor for `ReadPageGuard`. This destructor simply calls `Drop()`. */
ReadPageGuard::~ReadPageGuard() { Drop(); }

/**********************************************************************************************************************/
/**********************************************************************************************************************/
/**********************************************************************************************************************/

/**
 * @brief The only constructor for an RAII `WritePageGuard` that creates a valid guard.
 *
 * Note that only the buffer pool manager is allowed to call this constructor.
 *
 * TODO(P1): Add implementation.
 *
 * @param page_id The page ID of the page we want to write to.
 * @param frame A shared pointer to the frame that holds the page we want to protect.
 * @param replacer A shared pointer to the buffer pool manager's replacer.
 * @param bpm_latch A shared pointer to the buffer pool manager's latch.
 */
WritePageGuard::WritePageGuard(page_id_t page_id, std::shared_ptr<FrameHeader> frame,
                               std::shared_ptr<LRUKReplacer> replacer, std::shared_ptr<std::mutex> bpm_latch)
    : page_id_(page_id),
      frame_(std::move(frame)),
      replacer_(std::move(replacer)),
      bpm_latch_(std::move(bpm_latch)),
      is_valid_(true) {
  // 在p2中进行并发测试的时候发现的bug，应当直接在rwlatch加锁前解锁bpm_latch_
  bpm_latch_->unlock();
  // std::cout << "Writer "
  //         << " try to lock " << page_id << std::endl;
  frame_->rwlatch_.lock();
  // std::cout << "Writer "
  //         << " locked " << page_id << std::endl;
  frame_->is_dirty_ = true;
  // std::cout << "Thread " << std::this_thread::get_id()
  //         << " locked " << page_id << std::endl;
  // frame_->pin_count_++;
  // frame_->page_id_ = page_id;
  // replacer_->RecordAccess(frame_->frame_id_);
  // replacer_->SetEvictable(frame_->frame_id_, false);
}

/**
 * @brief The move constructor for `WritePageGuard`.
 *
 * ### Implementation
 *
 * If you are unfamiliar with move semantics, please familiarize yourself with learning materials online. There are many
 * great resources (including articles, Microsoft tutorials, YouTube videos) that explain this in depth.
 *
 * Make sure you invalidate the other guard, otherwise you might run into double free problems! For both objects, you
 * need to update _at least_ 5 fields each.
 *
 * TODO(P1): Add implementation.
 *
 * @param that The other page guard.
 */
WritePageGuard::WritePageGuard(WritePageGuard &&that) noexcept {
  if (this == &that) {
    return;
  }
  Drop();  // 这一行容易漏，是在vector的erase函数test中发现的问题，很难找到
  page_id_ = that.page_id_;
  frame_ = std::move(that.frame_);
  replacer_ = std::move(that.replacer_);
  bpm_latch_ = std::move(that.bpm_latch_);
  is_valid_ = that.is_valid_;
  that.is_valid_ = false;
}

/**
 * @brief The move assignment operator for `WritePageGuard`.
 *
 * ### Implementation
 *
 * If you are unfamiliar with move semantics, please familiarize yourself with learning materials online. There are many
 * great resources (including articles, Microsoft tutorials, YouTube videos) that explain this in depth.
 *
 * Make sure you invalidate the other guard, otherwise you might run into double free problems! For both objects, you
 * need to update _at least_ 5 fields each, and for the current object, make sure you release any resources it might be
 * holding on to.
 *
 * TODO(P1): Add implementation.
 *
 * @param that The other page guard.
 * @return WritePageGuard& The newly valid `WritePageGuard`.
 */
auto WritePageGuard::operator=(WritePageGuard &&that) noexcept -> WritePageGuard & {
  if (this == &that) {
    return *this;
  }
  Drop();  // 这一行容易漏，是在vector的erase函数test中发现的问题，很难找到
  page_id_ = that.page_id_;
  frame_ = std::move(that.frame_);
  replacer_ = std::move(that.replacer_);
  bpm_latch_ = std::move(that.bpm_latch_);
  is_valid_ = that.is_valid_;
  that.is_valid_ = false;
  // that不要Drop()，对应页面不要解锁，只是将页面管理权给了新的 WritePageGuard，也就是*this。对应页面依旧在上锁状态
  return *this;
}

/**
 * @brief Gets the page ID of the page this guard is protecting.
 */
auto WritePageGuard::GetPageId() const -> page_id_t {
  BUSTUB_ENSURE(is_valid_, "tried to use an invalid write guard");
  return page_id_;
}

/**
 * @brief Gets a `const` pointer to the page of data this guard is protecting.
 */
auto WritePageGuard::GetData() const -> const char * {
  BUSTUB_ENSURE(is_valid_, "tried to use an invalid write guard");
  return frame_->GetData();
}

/**
 * @brief Gets a mutable pointer to the page of data this guard is protecting.
 */
auto WritePageGuard::GetDataMut() -> char * {
  BUSTUB_ENSURE(is_valid_, "tried to use an invalid write guard");
  return frame_->GetDataMut();
}

/**
 * @brief Returns whether the page is dirty (modified but not flushed to the disk).
 */
auto WritePageGuard::IsDirty() const -> bool {
  BUSTUB_ENSURE(is_valid_, "tried to use an invalid write guard");
  return frame_->is_dirty_;
}

/**
 * @brief Manually drops a valid `WritePageGuard`'s data. If this guard is invalid, this function does nothing.
 *
 * ### Implementation
 *
 * Make sure you don't double free! Also, think **very** **VERY** carefully about what resources you own and the order
 * in which you release those resources. If you get the ordering wrong, you will very likely fail one of the later
 * Gradescope tests. You may also want to take the buffer pool manager's latch in a very specific scenario...
 *
 * TODO(P1): Add implementation.
 *
 *
 */
void WritePageGuard::Drop() {
  if (!is_valid_) {
    return;
  }

  // 这里同样需要保护buffer pool manager中的数据结构，需要修改的地方就要进行多线程保护
  bpm_latch_->lock();
  frame_->pin_count_--;
  if (frame_->pin_count_.load() == 0) {
    replacer_->SetEvictable(frame_->frame_id_, true);
  }
  bpm_latch_->unlock();

  frame_->rwlatch_.unlock();
  // std::cout << "Writer "
  //         << " unlocked " << page_id_ << std::endl;
  is_valid_ = false;
  replacer_ = nullptr;
  frame_ = nullptr;
}

/** @brief The destructor for `WritePageGuard`. This destructor simply calls `Drop()`. */
WritePageGuard::~WritePageGuard() { Drop(); }

}  // namespace bustub
