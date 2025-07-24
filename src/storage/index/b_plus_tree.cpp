#include "storage/index/b_plus_tree.h"
#include "storage/index/b_plus_tree_debug.h"

namespace bustub {

INDEX_TEMPLATE_ARGUMENTS
BPLUSTREE_TYPE::BPlusTree(std::string name, page_id_t header_page_id, BufferPoolManager *buffer_pool_manager,
                          const KeyComparator &comparator, int leaf_max_size, int internal_max_size)
    : index_name_(std::move(name)),
      bpm_(buffer_pool_manager),
      comparator_(std::move(comparator)),
      leaf_max_size_(leaf_max_size),
      internal_max_size_(internal_max_size),
      header_page_id_(header_page_id) {
  WritePageGuard guard = bpm_->WritePage(header_page_id_);
  auto root_page = guard.AsMut<BPlusTreeHeaderPage>();
  root_page->root_page_id_ = INVALID_PAGE_ID;
}

/*
 * Helper function to decide whether current b+tree is empty
 */
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::IsEmpty() const -> bool {
  ReadPageGuard guard = bpm_->ReadPage(header_page_id_);
  auto head_page = guard.As<BPlusTreeHeaderPage>();
  return head_page->root_page_id_ == INVALID_PAGE_ID;
}

/**
 * 使用二分查找进行键查找的函数
 * 内部节点与叶子节点的查找方式和返回形式有所不同
 * @return 键对应的值在数组中的index(叶子结点) or 键对应的下一层要查找的page_id在值数组中的index(内部结点)
 */
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::KeyBinarySearch(const BPlusTreePage *page, const KeyType &key) -> int {
  int l;
  int r;

  if (page->IsLeafPage()) {
    auto leaf_page = static_cast<const LeafPage *>(page);
    l = 0, r = leaf_page->GetSize() - 1;
    while (l <= r) {
      int mid = (l + r) >> 1;
      if (comparator_(key, leaf_page->KeyAt(mid)) == 0) {
        return mid;
      }
      if (comparator_(key, leaf_page->KeyAt(mid)) < 0) {
        r = mid - 1;
      } else {
        l = mid + 1;
      }
    }
  } else {
    auto internal_page = static_cast<const InternalPage *>(page);
    // 注意这里 l 要为 1
    l = 1, r = internal_page->GetSize() - 1;
    int size = internal_page->GetSize();
    // 内部节点的一个特殊情况，考虑key小于结点中第一个键的情况
    if (comparator_(key, internal_page->KeyAt(l)) < 0) {
      return 0;
    }
    while (l <= r) {
      int mid = (l + r) >> 1;
      if (comparator_(internal_page->KeyAt(mid), key) <= 0) {
        if (mid + 1 >= size || comparator_(internal_page->KeyAt(mid + 1), key) > 0) {
          return mid;
        }
        l = mid + 1;
      } else {
        r = mid - 1;
      }
    }  // test : 2 3 4 5 6 8 9 12 14
  }

  return -1;
}

/**
 * insert时查找叶子结点中插入位置的函数
 */
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::IndexBinarySearchLeaf(LeafPage *page, const KeyType &key) -> int {
  int l;
  int r;

  l = 0, r = page->GetSize() - 1;
  int size = page->GetSize();
  if (comparator_(key, page->KeyAt(l)) < 0) {
    return 0;
  }
  while (l <= r) {
    int mid = (l + r) >> 1;
    if (comparator_(page->KeyAt(mid), key) < 0) {
      if (mid + 1 >= size || comparator_(page->KeyAt(mid + 1), key) >= 0) {
        return mid + 1;
      }
      l = mid + 1;
    } else {
      r = mid - 1;
    }
  }

  return -1;
}

/**
 * 向左sibling结点借用键值的函数
 */
INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::BorrowFromLeft(BPlusTreePage *page, BPlusTreePage *left_page, BPlusTreePage *parent_page,
                                    int index) {
  // parent_page必然是内部结点，先将其进行转换
  auto parent_internal_page = static_cast<InternalPage *>(parent_page);
  int size = page->GetSize();
  int left_size = left_page->GetSize();

  if (page->IsLeafPage()) {
    auto leaf_page = static_cast<LeafPage *>(page);
    auto left_leaf_page = static_cast<LeafPage *>(left_page);

    // 从左边开始遍历移动还是从右边，一定要想清楚，不然会出错！
    for (int i = size - 1; i >= 0; i--) {
      leaf_page->SetKeyAt(i + 1, leaf_page->KeyAt(i));
      leaf_page->SetValueAt(i + 1, leaf_page->ValueAt(i));
    }
    leaf_page->SetKeyAt(0, left_leaf_page->KeyAt(left_size - 1));
    leaf_page->SetValueAt(0, left_leaf_page->ValueAt(left_size - 1));
    left_leaf_page->SetSize(left_size - 1);
    leaf_page->SetSize(size + 1);
    parent_internal_page->SetKeyAt(index, leaf_page->KeyAt(0));
  } else {
    auto internal_page = static_cast<InternalPage *>(page);
    auto left_internal_page = static_cast<InternalPage *>(left_page);

    for (int i = size - 1; i >= 0; i--) {
      if (i > 0) {
        internal_page->SetKeyAt(i + 1, internal_page->KeyAt(i));
      }
      internal_page->SetValueAt(i + 1, internal_page->ValueAt(i));
    }
    internal_page->SetKeyAt(1, parent_internal_page->KeyAt(index));
    internal_page->SetValueAt(0, left_internal_page->ValueAt(left_size - 1));
    parent_internal_page->SetKeyAt(index, left_internal_page->KeyAt(left_size - 1));
    left_internal_page->SetSize(left_size - 1);
    internal_page->SetSize(size + 1);
  }
}

/**
 * 向右sibling结点借用键值的函数
 */
INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::BorrowFromRight(BPlusTreePage *page, BPlusTreePage *right_page, BPlusTreePage *parent_page,
                                     int index) {
  // parent_page必然是内部结点，先将其进行转换
  auto parent_internal_page = static_cast<InternalPage *>(parent_page);
  int size = page->GetSize();
  int right_size = right_page->GetSize();

  if (page->IsLeafPage()) {
    auto leaf_page = static_cast<LeafPage *>(page);
    auto right_leaf_page = static_cast<LeafPage *>(right_page);

    leaf_page->SetKeyAt(size, right_leaf_page->KeyAt(0));
    leaf_page->SetValueAt(size, right_leaf_page->ValueAt(0));
    for (int i = 0; i < right_size - 1; i++) {
      right_leaf_page->SetKeyAt(i, right_leaf_page->KeyAt(i + 1));
      right_leaf_page->SetValueAt(i, right_leaf_page->ValueAt(i + 1));
    }
    right_leaf_page->SetSize(right_size - 1);
    leaf_page->SetSize(size + 1);
    parent_internal_page->SetKeyAt(index + 1, right_leaf_page->KeyAt(0));
  } else {
    auto internal_page = static_cast<InternalPage *>(page);
    auto right_internal_page = static_cast<InternalPage *>(right_page);

    internal_page->SetKeyAt(size, parent_internal_page->KeyAt(index + 1));
    internal_page->SetValueAt(size, right_internal_page->ValueAt(0));
    parent_internal_page->SetKeyAt(index + 1, right_internal_page->KeyAt(1));
    for (int i = 0; i < right_size; i++) {
      if (i > 0) {
        right_internal_page->SetKeyAt(i, right_internal_page->KeyAt(i + 1));
      }
      right_internal_page->SetValueAt(i, right_internal_page->ValueAt(i + 1));
    }
    right_internal_page->SetSize(right_size - 1);
    internal_page->SetSize(size + 1);
  }
}

/**
 * 与左sibling结点合并的函数
 */
INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::MergeWithLeft(BPlusTreePage *page, BPlusTreePage *left_page, BPlusTreePage *parent_page,
                                   int index) {
  int left_size = left_page->GetSize();
  int size = page->GetSize();
  int parent_size = parent_page->GetSize();
  auto parent_internal_page = static_cast<InternalPage *>(parent_page);

  if (page->IsLeafPage()) {
    auto leaf_page = static_cast<LeafPage *>(page);
    auto left_leaf_page = static_cast<LeafPage *>(left_page);
    for (int i = 0; i < size; i++) {
      left_leaf_page->SetKeyAt(i + left_size, leaf_page->KeyAt(i));
      left_leaf_page->SetValueAt(i + left_size, leaf_page->ValueAt(i));
    }
    // 随时记得更新结点的size
    left_leaf_page->SetSize(left_size + size);
    // 要记得更新next_page_id ！很容易漏
    // 在迭代器部分会检测这里是否正确进行了更新
    left_leaf_page->SetNextPageId(leaf_page->GetNextPageId());
  } else {
    auto internal_page = static_cast<InternalPage *>(page);
    auto left_internal_page = static_cast<InternalPage *>(left_page);
    KeyType middle_key = parent_internal_page->KeyAt(index);
    left_internal_page->SetKeyAt(left_size, middle_key);
    left_internal_page->SetValueAt(left_size, internal_page->ValueAt(0));
    for (int i = 1; i < size; i++) {
      left_internal_page->SetKeyAt(i + left_size, internal_page->KeyAt(i));
      left_internal_page->SetValueAt(i + left_size, internal_page->ValueAt(i));
    }
    left_internal_page->SetSize(left_size + size);
  }

  // 处理父结点
  for (int i = index; i < parent_size - 1; i++) {
    parent_internal_page->SetKeyAt(i, parent_internal_page->KeyAt(i + 1));
    parent_internal_page->SetValueAt(i, parent_internal_page->ValueAt(i + 1));
  }
  parent_internal_page->SetSize(parent_size - 1);
}

/**
 * 与右sibling结点合并的函数
 */
INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::MergeWithRight(BPlusTreePage *page, BPlusTreePage *right_page, BPlusTreePage *parent_page,
                                    int index) {
  int right_size = right_page->GetSize();
  int size = page->GetSize();
  int parent_size = parent_page->GetSize();
  auto parent_internal_page = static_cast<InternalPage *>(parent_page);

  if (page->IsLeafPage()) {
    auto leaf_page = static_cast<LeafPage *>(page);
    auto right_leaf_page = static_cast<LeafPage *>(right_page);
    for (int i = 0; i < right_size; i++) {
      leaf_page->SetKeyAt(i + size, right_leaf_page->KeyAt(i));
      leaf_page->SetValueAt(i + size, right_leaf_page->ValueAt(i));
    }
    // 随时记得更新结点的size
    leaf_page->SetSize(right_size + size);
    // 要记得更新next_page_id ！很容易漏
    // 在迭代器中会检测这里是否更新
    leaf_page->SetNextPageId(right_leaf_page->GetNextPageId());
  } else {
    auto internal_page = static_cast<InternalPage *>(page);
    auto right_internal_page = static_cast<InternalPage *>(right_page);
    KeyType middle_key = parent_internal_page->KeyAt(index + 1);
    // 之前对于size的设置出现了问题，导致出现了size为0的情况进行key的设置，触发了exception
    internal_page->SetKeyAt(size, middle_key);
    internal_page->SetValueAt(size, right_internal_page->ValueAt(0));
    for (int i = 1; i < right_size; i++) {
      internal_page->SetKeyAt(i + size, right_internal_page->KeyAt(i));
      internal_page->SetValueAt(i + size, right_internal_page->ValueAt(i));
    }
    internal_page->SetSize(right_size + size);
  }

  // 处理父结点
  for (int i = index + 1; i < parent_size - 1; i++) {
    parent_internal_page->SetKeyAt(i, parent_internal_page->KeyAt(i + 1));
    parent_internal_page->SetValueAt(i, parent_internal_page->ValueAt(i + 1));
  }
  parent_internal_page->SetSize(parent_size - 1);
}

/*****************************************************************************
 * SEARCH
 *****************************************************************************/
/**
 * Return the only value that associated with input key
 * This method is used for point query(点查询)
 * @return : true means key exists
 */
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::GetValue(const KeyType &key, std::vector<ValueType> *result) -> bool {
  // Declaration of context instance.
  Context ctx;

  ReadPageGuard guard = bpm_->ReadPage(header_page_id_);
  auto head_page = guard.As<BPlusTreeHeaderPage>();
  ctx.root_page_id_ = head_page->root_page_id_;
  guard.Drop();

  if (ctx.root_page_id_ == INVALID_PAGE_ID) {
    return false;
  }

  /* 当前结点为内部结点时 */
  ctx.read_set_.push_back(bpm_->ReadPage(ctx.root_page_id_));
  auto page = ctx.read_set_.back().As<BPlusTreePage>();
  while (!page->IsLeafPage()) {
    int index = KeyBinarySearch(page, key);
    if (index == -1) {
      return false;
    }
    auto internal_page = static_cast<const InternalPage *>(page);
    page_id_t page_id = internal_page->ValueAt(index);
    ctx.read_set_.push_back(bpm_->ReadPage(page_id));
    page = ctx.read_set_.back().As<BPlusTreePage>();
    ctx.read_set_.pop_front();
  }

  /* 当前结点为叶子结点时 */
  int index = KeyBinarySearch(page, key);
  if (index == -1) {
    return false;
  }
  auto leaf_page = static_cast<const LeafPage *>(page);
  result->push_back(leaf_page->ValueAt(index));

  // (void)ctx;
  return true;
}

/*****************************************************************************
 * INSERTION
 *****************************************************************************/
/**
 * Insert constant key & value pair into b+ tree
 * if current tree is empty, start new tree, update root page id and insert
 * entry, otherwise insert into leaf page.
 * @return: since we only support unique key, if user try to insert duplicate
 * keys return false, otherwise return true.
 */
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::Insert(const KeyType &key, const ValueType &value) -> bool {
  // Declaration of context instance.
  Context ctx;

  WritePageGuard tmp_head_guard = bpm_->WritePage(header_page_id_);
  ctx.header_page_ = std::make_optional(std::move(tmp_head_guard));
  ctx.root_page_id_ = ctx.header_page_->As<BPlusTreeHeaderPage>()->root_page_id_;
  // std::cout << "Thread " << std::this_thread::get_id() << " gained header page in start, root page id: " <<
  // ctx.root_page_id_ << std::endl;

  /* (1) 如果tree是空的 */
  if (ctx.root_page_id_ == INVALID_PAGE_ID) {
    // 创建根节点，根节点不需要遵循最小size原则。同时该根节点也是叶子节点
    page_id_t root_page_id = bpm_->NewPage();
    WritePageGuard root_guard = bpm_->WritePage(root_page_id);
    auto root_page = root_guard.AsMut<LeafPage>();
    root_page->Init(leaf_max_size_);
    root_page->SetKeyAt(0, key);
    root_page->SetValueAt(0, value);

    // 要随时记得修改结点的size_值
    root_page->SetSize(1);

    auto head_page = ctx.header_page_->AsMut<BPlusTreeHeaderPage>();
    head_page->root_page_id_ = root_page_id;
    return true;
  }

  /* (2) 如果tree不是空的 */
  /* (2.1) 乐观锁 */
  /* (2.1.1) 首先找到要进行插入操作的叶子结点 */
  BPlusTreePage *op_write_page = nullptr;
  ctx.read_set_.push_back(bpm_->ReadPage(ctx.root_page_id_));
  auto op_page = ctx.read_set_.back().As<BPlusTreePage>();
  // 如果root结点为叶子结点，则将其升级为写锁。这里存在时间空窗，但有header结点锁未释放，提供了线程保护
  if (op_page->IsLeafPage()) {
    ctx.read_set_.pop_back();
    ctx.write_set_.push_back(bpm_->WritePage(ctx.root_page_id_));
    op_page = ctx.write_set_.back().As<BPlusTreePage>();
  }
  ctx.header_page_ = std::nullopt;
  // std::cout << "Thread " << std::this_thread::get_id() << " released header page in op, root page id: " <<
  // ctx.root_page_id_ << std::endl; 用于在需要获取叶子结点的WritePageGuard时的操作。若为root page
  // id，则表示根节点就是叶子结点
  page_id_t page_id = ctx.root_page_id_;

  while (!op_page->IsLeafPage()) {
    int index = KeyBinarySearch(op_page, key);
    if (index == -1) {
      return false;
    }
    auto internal_page = static_cast<const InternalPage *>(op_page);
    page_id = internal_page->ValueAt(index);
    // 要记得维护ctx对象
    // latch crabbing
    ctx.read_set_.push_back(bpm_->ReadPage(page_id));
    op_page = ctx.read_set_.back().As<BPlusTreePage>();
    // 如果当前结点为叶子结点，则在其父结点锁未被释放的情况下，进行读锁向写锁的升级
    // 父结点锁未被释放，保证读写锁升级过程的线程安全
    if (op_page->IsLeafPage()) {
      ctx.read_set_.pop_back();
      ctx.write_set_.push_back(bpm_->WritePage(page_id));
      op_page = op_page = ctx.write_set_.back().As<BPlusTreePage>();
    }
    ctx.read_set_.pop_front();
  }

  // 如果叶子结点无需合并以及借用，则获取WritePageGuard后直接进行delete
  // 这段代码出现过多线程bug，主要问题在于在if判断之后才进行WritePageGuard的获取，导致多个线程都已经通过if判断，但之后某个线程先进行了操作，对页面的size进行了增加，导致另一个线程获取WritePageGuard时，页面size已经超出了maxsize，导致HeapBuffer
  // Overflow（堆缓冲区溢出） 解决问题的关键在于要在判断if之前将WritePageGuard获取

  // 这里还出现过一个问题：释放叶子结点的读锁后，在获取写锁之前存在时间空窗，在此时间可能被其他线程获得写锁，修改了叶子结点之后，本线程才获得写锁，此时叶子结点已经被修改。之后的操作也会存在数据一致性问题
  // 因此应该修改为直接获取写锁，不应该设置读写锁升级过程，避免时间空窗
  // ctx.read_set_.clear();
  // ctx.write_set_.push_back(bpm_->WritePage(page_id));
  // op_write_page = ctx.write_set_.back().AsMut<BPlusTreePage>();

  op_write_page = ctx.write_set_.back().AsMut<BPlusTreePage>();

  if (op_write_page->GetSize() < op_write_page->GetMaxSize()) {
    auto leaf_page = static_cast<LeafPage *>(op_write_page);
    int insert_index = IndexBinarySearchLeaf(leaf_page, key);

    // std::cout << "Thread " << std::this_thread::get_id() << " inserted value: " << value.GetSlotNum() << " inserted
    // page_id : " << ctx.write_set_.back().GetPageId() <<
    //   " inserted index :" << insert_index << " in op" << std::endl;

    // 返回-1表示没有满足条件的插入位置，一般为右边界等于key的情况
    if (insert_index == -1) {
      // std::cout << "Thread " << std::this_thread::get_id() << " can't insert value: " << value.GetSlotNum()  <<
      // std::endl;
      return false;
    }
    // 前面函数找到的index位置可能键与key相同，若相同则返回false
    if (comparator_(leaf_page->KeyAt(insert_index), key) == 0) {
      // std::cout << "Thread " << std::this_thread::get_id() << " can't insert value: " << value.GetSlotNum()  <<
      // std::endl;
      return false;
    }

    int size = leaf_page->GetSize();
    for (int i = size; i > insert_index; i--) {
      leaf_page->SetKeyAt(i, leaf_page->KeyAt(i - 1));
      leaf_page->SetValueAt(i, leaf_page->ValueAt(i - 1));
      // std::cout << "i = " << i << std::endl;
    }
    leaf_page->SetKeyAt(insert_index, key);
    leaf_page->SetValueAt(insert_index, value);
    // 随时记得修改结点size_
    leaf_page->SetSize(size + 1);
    // std::cout << "Thread " << std::this_thread::get_id() << " finished inserting " << value.GetSlotNum()  << " in op"
    // << std::endl;
    return true;
  }
  // 若不符合条件，则将WritePageGuard释放
  ctx.write_set_.clear();

  /* (2.2) 悲观锁 latch crabbing */
  /* (2.2.1) 首先找到要插入的叶子结点 */
  WritePageGuard head_guard = bpm_->WritePage(header_page_id_);
  ctx.header_page_ = std::make_optional(std::move(head_guard));
  // 重新获得一次root结点page
  // id，之前在test中出现过多线程问题，主要问题在于其他线程创建了新root结点，header被修改了root_page_id，但是这里用的root_page_id依旧是函数最开始时获取的
  ctx.root_page_id_ = ctx.header_page_->As<BPlusTreeHeaderPage>()->root_page_id_;
  // std::cout << "Thread " << std::this_thread::get_id() << " gained header page in pe, root page id: " <<
  // ctx.root_page_id_ << std::endl;

  WritePageGuard write_page_guard = bpm_->WritePage(ctx.root_page_id_);
  auto page = write_page_guard.AsMut<BPlusTreePage>();
  ctx.write_set_.push_back(std::move(write_page_guard));
  // latch grabbing
  if (page->GetSize() < page->GetMaxSize()) {
    ctx.header_page_ = std::nullopt;
  }

  while (!page->IsLeafPage()) {
    int index = KeyBinarySearch(page, key);
    if (index == -1) {
      return false;
    }
    auto internal_page = static_cast<InternalPage *>(page);
    page_id_t page_id = internal_page->ValueAt(index);
    // 要记得维护ctx对象
    ctx.write_set_.push_back(bpm_->WritePage(page_id));
    // std::cout << "Thread " << std::this_thread::get_id() << " locked page " << page_id << std::endl;
    // 个人觉得需要在context类中加入存放内部结点搜索位置index的数组
    ctx.indexes_.push_back(index);
    page = ctx.write_set_.back().AsMut<BPlusTreePage>();
    if (page->GetSize() < page->GetMaxSize()) {
      // 现将 header_page 释放
      if (ctx.header_page_.has_value()) {
        ctx.header_page_ = std::nullopt;
      }
      while (ctx.write_set_.size() > 1) {
        // page_id_t tmp = ctx.write_set_.front().GetPageId();
        // std::cout << "Thread " << std::this_thread::get_id() << " unlocked page " << tmp << std::endl;
        ctx.write_set_.pop_front();
      }
    }
  }

  /* (2.2.2) 之后找到要插入key的位置 */
  auto leaf_page = static_cast<LeafPage *>(page);
  int insert_index = IndexBinarySearchLeaf(leaf_page, key);

  // std::cout << "Thread " << std::this_thread::get_id() << " inserted value: " << value.GetSlotNum() << " inserted
  // page_id : " << ctx.write_set_.back().GetPageId() <<
  //     " inserted index :" << insert_index << " in pe" << std::endl;

  // 返回-1表示没有满足条件的插入位置，一般为右边界等于key的情况
  if (insert_index == -1) {
    // std::cout << "Thread " << std::this_thread::get_id() << " can't insert value: " << value.GetSlotNum()  <<
    // std::endl;
    return false;
  }
  // 前面函数找到的index位置可能键与key相同，若相同则返回false
  if (comparator_(leaf_page->KeyAt(insert_index), key) == 0) {
    // std::cout << "Thread " << std::this_thread::get_id() << " can't insert value: " << value.GetSlotNum() <<
    // std::endl;
    return false;
  }

  /* (2.2.3) 若要插入的叶子结点未满，则直接插入 */
  if (leaf_page->GetSize() < leaf_page->GetMaxSize()) {
    // 这种情况下，之前加入的内部结点guard不需要再使用
    // 将其释放，增加并发效率
    // while (ctx.write_set_.size() > 1) {
    //   ctx.write_set_.pop_front();
    // }

    int size = leaf_page->GetSize();
    for (int i = size; i > insert_index; i--) {
      leaf_page->SetKeyAt(i, leaf_page->KeyAt(i - 1));
      leaf_page->SetValueAt(i, leaf_page->ValueAt(i - 1));
    }
    leaf_page->SetKeyAt(insert_index, key);
    leaf_page->SetValueAt(insert_index, value);
    // 随时记得修改结点size_
    leaf_page->SetSize(size + 1);
    ctx.write_set_.pop_front();
    // std::cout << "Thread " << std::this_thread::get_id() << " finished inserting " << value.GetSlotNum()  << " in pe"
    // << std::endl;
    return true;
  }

  /* (2.2.4) 若要插入的叶子结点已满，则需要进行split */
  // 这里实际是 (maxsize + 1) / 2 的向上取整，相当于(maxsize + 1 + 1) / 2 的向下取整
  // 这里设置让分裂后第一个结点的键数量为(maxsize + 1) / 2 的向上取整，也就是分裂后第一个叶子有时会比第二个多一个键值对
  int first_size = (leaf_page->GetMaxSize() + 2) / 2;
  int second_size = leaf_page->GetMaxSize() + 1 - first_size;
  page_id_t new_leaf_id = bpm_->NewPage();
  WritePageGuard new_leaf_guard = bpm_->WritePage(new_leaf_id);
  auto new_leaf_page = new_leaf_guard.AsMut<LeafPage>();
  ctx.write_set_.push_back(std::move(new_leaf_guard));
  new_leaf_page->Init(leaf_max_size_);
  // 随时记得更新各结点size_
  new_leaf_page->SetSize(second_size);
  leaf_page->SetSize(first_size);
  // 记得修改原叶子节点和新叶子结点的next_page_id_
  new_leaf_page->SetNextPageId(leaf_page->GetNextPageId());
  leaf_page->SetNextPageId(new_leaf_id);

  // 分插入位置在老叶子结点还是新叶子结点来分别处理
  if (insert_index < first_size) {
    for (int i = 0; i < second_size; i++) {
      new_leaf_page->SetKeyAt(i, leaf_page->KeyAt(i + first_size - 1));
      new_leaf_page->SetValueAt(i, leaf_page->ValueAt(i + first_size - 1));
    }
    for (int i = first_size - 1; i > insert_index; i--) {
      leaf_page->SetKeyAt(i, leaf_page->KeyAt(i - 1));
      leaf_page->SetValueAt(i, leaf_page->ValueAt(i - 1));
    }
    leaf_page->SetKeyAt(insert_index, key);
    leaf_page->SetValueAt(insert_index, value);
  } else {
    for (int i = 0; i < insert_index - first_size; i++) {
      new_leaf_page->SetKeyAt(i, leaf_page->KeyAt(i + first_size));
      new_leaf_page->SetValueAt(i, leaf_page->ValueAt(i + first_size));
    }
    new_leaf_page->SetKeyAt(insert_index - first_size, key);
    new_leaf_page->SetValueAt(insert_index - first_size, value);
    for (int i = insert_index - first_size + 1; i < second_size; i++) {
      new_leaf_page->SetKeyAt(i, leaf_page->KeyAt(i + first_size - 1));
      new_leaf_page->SetValueAt(i, leaf_page->ValueAt(i + first_size - 1));
    }
  }

  /* (2.2.5) 处理完叶子结点split操作后，继续更新内部结点 */
  // 获取要插入上一级内部结点的key，即新叶子结点的第一个key
  KeyType insert_key = new_leaf_page->KeyAt(0);
  // 两个叶子结点都处理完了，释放guard
  ctx.write_set_.pop_back();
  ctx.write_set_.pop_back();
  // 用于保存分裂后两个结点的page id
  // 第一个其实可以不定义，主要是根结点分裂时使用
  page_id_t first_split_page_id = ctx.root_page_id_;
  page_id_t second_split_page_id = new_leaf_id;
  // 用于判断是否需要创建新的根结点
  bool new_root_flag = true;

  while (!ctx.write_set_.empty()) {
    int insert_index = ctx.indexes_.back() + 1;
    auto internal_page = ctx.write_set_.back().AsMut<InternalPage>();
    int size = internal_page->GetSize();

    if (size < internal_page->GetMaxSize()) {
      for (int i = size; i > insert_index; i--) {
        internal_page->SetKeyAt(i, internal_page->KeyAt(i - 1));
        internal_page->SetValueAt(i, internal_page->ValueAt(i - 1));
      }
      internal_page->SetKeyAt(insert_index, insert_key);
      internal_page->SetValueAt(insert_index, second_split_page_id);
      // 随时记得更新各结点size_
      internal_page->SetSize(size + 1);
      new_root_flag = false;
      ctx.write_set_.clear();
      ctx.indexes_.clear();
      break;
    }

    // 当内部结点已满时，继续进行分裂
    // 这里的size是指value的数量，不是key的数量
    // 同样老结点size_向上取整，新结点向下取整
    int first_size = (internal_page->GetMaxSize() + 2) / 2;
    int second_size = internal_page->GetMaxSize() + 1 - first_size;
    page_id_t new_internal_id = bpm_->NewPage();
    WritePageGuard new_internal_guard = bpm_->WritePage(new_internal_id);
    auto new_internal_page = new_internal_guard.AsMut<InternalPage>();
    ctx.write_set_.push_back(std::move(new_internal_guard));
    new_internal_page->Init(internal_max_size_);
    // 随时记得更新各结点size_
    new_internal_page->SetSize(second_size);
    internal_page->SetSize(first_size);

    // 这里要注意，最中间的key是不需要保留在两个结点中的，会作为insert_key向一层传递，插入到上一层的internal page中
    // 最中间的key反映了右侧结点所在子树的最小值水平，所以向上传递，插入上层结点
    if (insert_index < first_size) {
      KeyType tmp_key = internal_page->KeyAt(first_size - 1);
      for (int i = 0; i < second_size; i++) {
        // index为0位置key不存在，要注意单独处理
        if (i > 0) {
          new_internal_page->SetKeyAt(i, internal_page->KeyAt(i + first_size - 1));
        }
        new_internal_page->SetValueAt(i, internal_page->ValueAt(i + first_size - 1));
      }
      for (int i = first_size - 1; i > insert_index; i--) {
        internal_page->SetKeyAt(i, internal_page->KeyAt(i - 1));
        internal_page->SetValueAt(i, internal_page->ValueAt(i - 1));
      }
      internal_page->SetKeyAt(insert_index, insert_key);
      internal_page->SetValueAt(insert_index, second_split_page_id);
      // 更新insert_key
      insert_key = tmp_key;
    } else {
      for (int i = 0; i < insert_index - first_size; i++) {
        if (i > 0) {
          new_internal_page->SetKeyAt(i, internal_page->KeyAt(i + first_size));
        }
        new_internal_page->SetValueAt(i, internal_page->ValueAt(i + first_size));
      }
      KeyType tmp_key;
      // 这里有过bug，主要原因是写法比较复杂，漏了insert_index正好为first_size的情况，即此时要往上传递的key即为insert_key，而不是internal_page索引为first_size的key
      if (insert_index > first_size) {
        new_internal_page->SetKeyAt(insert_index - first_size, insert_key);
        tmp_key = internal_page->KeyAt(first_size);
      } else {
        // 如果insert_index等于first_size
        tmp_key = insert_key;
      }
      new_internal_page->SetValueAt(insert_index - first_size, second_split_page_id);
      for (int i = insert_index - first_size + 1; i < second_size; i++) {
        new_internal_page->SetKeyAt(i, internal_page->KeyAt(i + first_size - 1));
        new_internal_page->SetValueAt(i, internal_page->ValueAt(i + first_size - 1));
      }
      // 更新insert_key
      insert_key = tmp_key;
    }
    // 更新要向上传递的新结点page id
    second_split_page_id = new_internal_id;
    // 释放新分裂结点的page guard
    ctx.write_set_.pop_back();

    // 最后要记得更新ctx
    ctx.write_set_.pop_back();
    ctx.indexes_.pop_back();
  }

  // 当需要创建新的root结点时，创建新结点并更新 header page
  if (new_root_flag) {
    page_id_t new_root_id = bpm_->NewPage();
    WritePageGuard new_root_guard = bpm_->WritePage(new_root_id);
    auto new_root_page = new_root_guard.AsMut<InternalPage>();
    ctx.write_set_.push_back(std::move(new_root_guard));

    new_root_page->Init(internal_max_size_);
    // 这里size_应该设置为2，因为internal page 的size_指的是value的数量，是key的数量加一
    new_root_page->SetSize(2);
    new_root_page->SetKeyAt(1, insert_key);
    new_root_page->SetValueAt(0, first_split_page_id);
    new_root_page->SetValueAt(1, second_split_page_id);

    auto head_page = ctx.header_page_->AsMut<BPlusTreeHeaderPage>();
    head_page->root_page_id_ = new_root_id;
    ctx.write_set_.clear();
  }

  // std::cout << "Thread " << std::this_thread::get_id() << " finished inserting " << value.GetSlotNum() << " in pe" <<
  // std::endl; (void)ctx;
  return true;
}

/*****************************************************************************
 * REMOVE
 *****************************************************************************/
/*
 * Delete key & value pair associated with input key
 * If current tree is empty, return immediately.
 * If not, User needs to first find the right leaf page as deletion target, then
 * delete entry from leaf page. Remember to deal with redistribute or merge if
 * necessary.
 */
INDEX_TEMPLATE_ARGUMENTS
void BPLUSTREE_TYPE::Remove(const KeyType &key) {
  // Declaration of context instance.
  Context ctx;

  ReadPageGuard read_head_guard = bpm_->ReadPage(header_page_id_);
  ctx.root_page_id_ = read_head_guard.As<BPlusTreeHeaderPage>()->root_page_id_;

  /* (1) 如果tree是空的 */
  if (ctx.root_page_id_ == INVALID_PAGE_ID) {
    return;
  }

  /* (2) 如果tree不是空的*/
  /* (2.1) 乐观锁 */
  /* (2.1.1) 首先找到要进行删除操作的叶子结点 */
  BPlusTreePage *op_write_page = nullptr;
  ctx.read_set_.push_back(bpm_->ReadPage(ctx.root_page_id_));
  auto op_page = ctx.read_set_.back().As<BPlusTreePage>();
  // 如果root结点为叶子结点，则将其升级为写锁。这里存在时间空窗，但有header结点锁未释放，提供了线程保护
  if (op_page->IsLeafPage()) {
    ctx.read_set_.pop_back();
    ctx.write_set_.push_back(bpm_->WritePage(ctx.root_page_id_));
    op_page = ctx.write_set_.back().As<BPlusTreePage>();
  }
  read_head_guard.Drop();
  page_id_t page_id = ctx.root_page_id_;

  while (!op_page->IsLeafPage()) {
    int index = KeyBinarySearch(op_page, key);
    if (index == -1) {
      return;
    }
    auto internal_page = static_cast<const InternalPage *>(op_page);
    page_id = internal_page->ValueAt(index);
    // 要记得维护ctx对象
    // latch crabbing
    ctx.read_set_.push_back(bpm_->ReadPage(page_id));
    op_page = ctx.read_set_.back().As<BPlusTreePage>();
    // 如果当前结点为叶子结点，则在其父结点锁未被释放的情况下，进行读锁向写锁的升级
    // 父结点锁未被释放，保证读写锁升级过程的线程安全
    if (op_page->IsLeafPage()) {
      ctx.read_set_.pop_back();
      ctx.write_set_.push_back(bpm_->WritePage(page_id));
      op_page = op_page = ctx.write_set_.back().As<BPlusTreePage>();
    }
    ctx.read_set_.pop_front();
  }

  op_write_page = ctx.write_set_.back().AsMut<BPlusTreePage>();

  // 如果叶子结点无需合并以及借用，则获取WritePageGuard后直接delete
  if (op_write_page->GetSize() > op_write_page->GetMinSize()) {
    int delete_index = KeyBinarySearch(op_write_page, key);
    if (delete_index == -1) {
      return;
    }

    int size = op_write_page->GetSize();
    auto leaf_page = static_cast<LeafPage *>(op_write_page);
    for (int i = delete_index; i < size - 1; i++) {
      leaf_page->SetKeyAt(i, leaf_page->KeyAt(i + 1));
      leaf_page->SetValueAt(i, leaf_page->ValueAt(i + 1));
    }
    // 随时记得修改结点size_
    leaf_page->SetSize(size - 1);
    return;
  }
  ctx.write_set_.clear();

  /* (2.2) 悲观锁 latch grabbing */
  /* (2.2.1) 首先找到要进行删除操作的叶子结点 */
  WritePageGuard head_guard = bpm_->WritePage(header_page_id_);
  ctx.header_page_ = std::make_optional(std::move(head_guard));
  // 重新获得一次root结点page
  // id，之前在test中出现过多线程问题，主要问题在于其他线程创建了新root结点，header被修改了root_page_id，但是这里用的root_page_id依旧是函数最开始时获取的
  ctx.root_page_id_ = ctx.header_page_->As<BPlusTreeHeaderPage>()->root_page_id_;

  WritePageGuard write_page_guard = bpm_->WritePage(ctx.root_page_id_);
  auto page = write_page_guard.AsMut<BPlusTreePage>();
  ctx.write_set_.push_back(std::move(write_page_guard));
  // latch grabbing
  // delete函数中，root结点最小值不是其他结点的minSize，其size需要大于2（即key数量大于1）
  if (page->GetSize() > 2) {
    ctx.header_page_ = std::nullopt;
  }

  while (!page->IsLeafPage()) {
    int index = KeyBinarySearch(page, key);
    if (index == -1) {
      return;
    }
    auto internal_page = static_cast<InternalPage *>(page);
    page_id_t page_id = internal_page->ValueAt(index);
    // 要记得维护ctx对象
    ctx.write_set_.push_back(bpm_->WritePage(page_id));
    // 个人觉得需要在context类中加入存放内部结点搜索位置index的数组
    ctx.indexes_.push_back(index);
    page = ctx.write_set_.back().AsMut<BPlusTreePage>();
    if (page->GetSize() > page->GetMinSize()) {
      if (ctx.header_page_.has_value()) {
        ctx.header_page_ = std::nullopt;
      }
      while (ctx.write_set_.size() > 1) {
        ctx.write_set_.pop_front();
      }
    }
  }

  /* (2.2.2) 之后找到要删除key的位置 */
  int delete_index = KeyBinarySearch(page, key);
  if (delete_index == -1) {
    return;
  }

  // 统一先将对应的key和value删除
  int size = page->GetSize();
  auto leaf_page = static_cast<LeafPage *>(page);
  for (int i = delete_index; i < size - 1; i++) {
    leaf_page->SetKeyAt(i, leaf_page->KeyAt(i + 1));
    leaf_page->SetValueAt(i, leaf_page->ValueAt(i + 1));
  }
  // 随时记得修改结点size_
  page->SetSize(size - 1);

  // 当前操作结点的page id，在原root变为空要被删除时，也是新root结点的page id，
  page_id_t now_page_id = INVALID_PAGE_ID;

  /* (2.2.3)
   * 若删除后的结点大于等于半满，则完成删除操作;若删除后的叶子结点小于半满，则先判断能否从sibling结点中借key。若可借则借完后修改父结点即可；若不能借则进行与sibling的合并，之后再向上判断内部结点情况。内部结点同理。由此设计向上迭代的处理过程
   */
  while (!ctx.write_set_.empty()) {
    // 如果此时结点为root结点
    if (ctx.write_set_.size() == 1) {
      auto root_page = ctx.write_set_.back().AsMut<BPlusTreePage>();
      // 如果root结点为叶子结点，则单独处理。如果root不为空，则直接return，如果为空，则重新设置root page id
      if (root_page->IsLeafPage()) {
        if (root_page->GetSize() == 0) {
          auto header_page = ctx.header_page_->AsMut<BPlusTreeHeaderPage>();
          header_page->root_page_id_ = INVALID_PAGE_ID;
        }
        return;
      }
      // 如果此时root结点为内部结点且为空，则将原root结点删除，且修改root结点的page id
      // size为1时，没有key存在，只有一个value，此时root为不合法状态，同样需要删除
      if (root_page->GetSize() <= 1) {
        ctx.write_set_.pop_back();
        bpm_->DeletePage(ctx.root_page_id_);
        auto header_page = ctx.header_page_->AsMut<BPlusTreeHeaderPage>();
        header_page->root_page_id_ = now_page_id;
      }
      // 若root结点不为空，则不用管minSize的约束，直接return
      return;
    }

    // 若删除后的结点大于等于半满，则完成删除操作。这里相当于递归的出口
    if (page->GetSize() >= page->GetMinSize()) {
      return;
    }

    // 使用反向迭代器获取deque倒数第二个元素，即当前处理元素的父结点
    auto it = ctx.write_set_.rbegin();
    ++it;
    auto parent_page = it->AsMut<InternalPage>();
    int index = ctx.indexes_.back();

    // 先判断是否可以左借用
    if (index > 0) {
      WritePageGuard left_guard = bpm_->WritePage(parent_page->ValueAt(index - 1));
      auto left_page = left_guard.AsMut<BPlusTreePage>();
      ctx.write_set_.push_back(std::move(left_guard));
      if (left_page->GetSize() > left_page->GetMinSize()) {
        BorrowFromLeft(page, left_page, parent_page, index);
        return;
      }
      // 如果没有进行左借用，记得把left_guard释放，不再占用对应页面
      ctx.write_set_.pop_back();
    }

    // 再判断是否可以右借用
    if (index < parent_page->GetSize() - 1) {
      WritePageGuard right_guard = bpm_->WritePage(parent_page->ValueAt(index + 1));
      auto right_page = right_guard.AsMut<BPlusTreePage>();
      ctx.write_set_.push_back(std::move(right_guard));
      if (right_page->GetSize() > right_page->GetMinSize()) {
        BorrowFromRight(page, right_page, parent_page, index);
        return;
      }
      // 如果没有进行右借用，记得把right_guard释放，不再占用对应页面
      ctx.write_set_.pop_back();
    }

    // 不可借用，则先判断是否可以左合并
    if (index > 0) {
      WritePageGuard left_guard = bpm_->WritePage(parent_page->ValueAt(index - 1));
      auto left_page = left_guard.AsMut<BPlusTreePage>();
      ctx.write_set_.push_back(std::move(left_guard));
      MergeWithLeft(page, left_page, parent_page, index);
      now_page_id = ctx.write_set_.back().GetPageId();
      ctx.write_set_.pop_back();
      // 将被合并的页面delete
      page_id_t page_id = ctx.write_set_.back().GetPageId();
      ctx.write_set_.pop_back();
      bpm_->DeletePage(page_id);
    } else {
      // 若不可左合并（即index为0时），则进行右合并
      WritePageGuard right_guard = bpm_->WritePage(parent_page->ValueAt(index + 1));
      auto right_page = right_guard.AsMut<BPlusTreePage>();
      ctx.write_set_.push_back(std::move(right_guard));
      MergeWithRight(page, right_page, parent_page, index);
      // 将被合并的页面delete，注意这里删除的是右边的结点
      page_id_t page_id = ctx.write_set_.back().GetPageId();
      ctx.write_set_.pop_back();
      now_page_id = ctx.write_set_.back().GetPageId();
      ctx.write_set_.pop_back();
      bpm_->DeletePage(page_id);
    }

    // 之前漏了这句，出现了很严重的bug，导致读取的page id错误，由此导致了递归加锁的情况
    ctx.indexes_.pop_back();
    page = ctx.write_set_.back().AsMut<BPlusTreePage>();
  }

  // (void)ctx;
}

/*****************************************************************************
 * INDEX ITERATOR
 *****************************************************************************/
/*
 * Input parameter is void, find the leftmost leaf page first, then construct
 * index iterator
 * @return : index iterator
 */
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::Begin() -> INDEXITERATOR_TYPE {
  Context ctx;

  ReadPageGuard guard = bpm_->ReadPage(header_page_id_);
  auto head_page = guard.As<BPlusTreeHeaderPage>();
  ctx.root_page_id_ = head_page->root_page_id_;
  guard.Drop();

  if (ctx.root_page_id_ == INVALID_PAGE_ID) {
    return INDEXITERATOR_TYPE(bpm_, INVALID_PAGE_ID, -1);
  }

  // 找到最左侧的叶子结点
  ReadPageGuard page_guard = bpm_->ReadPage(ctx.root_page_id_);
  auto page = page_guard.As<BPlusTreePage>();
  while (!page->IsLeafPage()) {
    auto internal_page = static_cast<const InternalPage *>(page);
    page_id_t page_id = internal_page->ValueAt(0);
    page_guard = bpm_->ReadPage(page_id);
    page = page_guard.As<BPlusTreePage>();
  }
  return INDEXITERATOR_TYPE(bpm_, page_guard.GetPageId(), 0);
}

/*
 * Input parameter is low key, find the leaf page that contains the input key
 * first, then construct index iterator
 * @return : index iterator
 */
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::Begin(const KeyType &key) -> INDEXITERATOR_TYPE {
  Context ctx;

  ReadPageGuard guard = bpm_->ReadPage(header_page_id_);
  auto head_page = guard.As<BPlusTreeHeaderPage>();
  ctx.root_page_id_ = head_page->root_page_id_;
  guard.Drop();

  if (ctx.root_page_id_ == INVALID_PAGE_ID) {
    return INDEXITERATOR_TYPE(bpm_, INVALID_PAGE_ID, -1);
  }

  ReadPageGuard page_guard = bpm_->ReadPage(ctx.root_page_id_);
  auto page = page_guard.As<BPlusTreePage>();
  while (!page->IsLeafPage()) {
    int index = KeyBinarySearch(page, key);
    auto internal_page = static_cast<const InternalPage *>(page);
    page_id_t page_id = internal_page->ValueAt(index);
    page_guard = bpm_->ReadPage(page_id);
    page = page_guard.As<BPlusTreePage>();
  }
  // 当前结点为叶子结点时
  int index = KeyBinarySearch(page, key);
  if (index == -1) {
    return INDEXITERATOR_TYPE(bpm_, INVALID_PAGE_ID, -1);
  }
  return INDEXITERATOR_TYPE(bpm_, page_guard.GetPageId(), index);
}

/*
 * Input parameter is void, construct an index iterator representing the end
 * of the key/value pair in the leaf node
 * @return : index iterator
 */
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::End() -> INDEXITERATOR_TYPE { return INDEXITERATOR_TYPE(bpm_, INVALID_PAGE_ID, -1); }

/**
 * @return Page id of the root of this tree
 */
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::GetRootPageId() -> page_id_t {
  ReadPageGuard guard = bpm_->ReadPage(header_page_id_);
  auto head_page = guard.As<BPlusTreeHeaderPage>();
  return head_page->root_page_id_;
}

template class BPlusTree<GenericKey<4>, RID, GenericComparator<4>>;

template class BPlusTree<GenericKey<8>, RID, GenericComparator<8>>;

template class BPlusTree<GenericKey<16>, RID, GenericComparator<16>>;

template class BPlusTree<GenericKey<32>, RID, GenericComparator<32>>;

template class BPlusTree<GenericKey<64>, RID, GenericComparator<64>>;

}  // namespace bustub
