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
  ReadPageGuard page_guard = bpm_->ReadPage(ctx.root_page_id_);
  auto page = page_guard.As<BPlusTreePage>();
  while (!page->IsLeafPage()) {
    int index = KeyBinarySearch(page, key);
    if (index == -1) {
      return false;
    }
    auto internal_page = static_cast<const InternalPage *>(page);
    page_id_t page_id = internal_page->ValueAt(index);
    page_guard = bpm_->ReadPage(page_id);
    page = page_guard.As<BPlusTreePage>();
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

  WritePageGuard head_guard = bpm_->WritePage(header_page_id_);
  ctx.header_page_ = std::make_optional(std::move(head_guard));
  ctx.root_page_id_ = ctx.header_page_->AsMut<BPlusTreeHeaderPage>()->root_page_id_;

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
  /* (2.1) 首先找到要插入的叶子结点 */
  WritePageGuard page_guard = bpm_->WritePage(ctx.root_page_id_);
  auto page = page_guard.AsMut<BPlusTreePage>();
  ctx.write_set_.push_back(std::move(page_guard));

  while (!page->IsLeafPage()) {
    int index = KeyBinarySearch(page, key);
    if (index == -1) {
      return false;
    }
    auto internal_page = static_cast<InternalPage *>(page);
    page_id_t page_id = internal_page->ValueAt(index);
    // 要记得维护ctx对象
    ctx.write_set_.push_back(bpm_->WritePage(page_id));
    // 个人觉得需要在context类中加入存放内部结点搜索位置index的数组
    ctx.indexes_.push_back(index);
    page = ctx.write_set_.back().AsMut<BPlusTreePage>();
  }

  /* (2.2) 之后找到要插入key的位置 */
  auto leaf_page = static_cast<LeafPage *>(page);
  int insert_index = IndexBinarySearchLeaf(leaf_page, key);
  // 前面函数找到的index位置可能键与key相同，若相同则返回false
  if (comparator_(leaf_page->KeyAt(insert_index), key) == 0) {
    return false;
  }

  /* (2.3) 若要插入的叶子结点未满，则直接插入 */
  if (leaf_page->GetSize() < leaf_page->GetMaxSize()) {
    // 这种情况下，之前加入的内部结点guard不需要再使用
    // 将其释放，增加并发效率
    while (ctx.write_set_.size() > 1) {
      ctx.write_set_.pop_front();
    }

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
    return true;
  }

  /* (2.4) 若要插入的叶子结点已满，则需要进行split */
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

  /* (2.5) 处理完叶子结点split操作后，继续更新内部结点 */
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
      KeyType tmp_key = internal_page->KeyAt(first_size);
      if (insert_index > first_size) {
        new_internal_page->SetKeyAt(insert_index - first_size, insert_key);
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
    ctx.write_set_.pop_back();
  }

  // (void)ctx;
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
  (void)ctx;
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
auto BPLUSTREE_TYPE::Begin() -> INDEXITERATOR_TYPE { return INDEXITERATOR_TYPE(); }

/*
 * Input parameter is low key, find the leaf page that contains the input key
 * first, then construct index iterator
 * @return : index iterator
 */
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::Begin(const KeyType &key) -> INDEXITERATOR_TYPE { return INDEXITERATOR_TYPE(); }

/*
 * Input parameter is void, construct an index iterator representing the end
 * of the key/value pair in the leaf node
 * @return : index iterator
 */
INDEX_TEMPLATE_ARGUMENTS
auto BPLUSTREE_TYPE::End() -> INDEXITERATOR_TYPE { return INDEXITERATOR_TYPE(); }

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
