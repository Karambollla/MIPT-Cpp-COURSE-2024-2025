#include <cstddef>
#include <iostream>
#include <iterator>
#include <memory>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

template <typename T, typename Allocator = std::allocator<T> >
class List {
  template <typename G, typename O, typename I, typename D, typename A>
  friend class UnorderedMap;

 private:
  struct Node {
    T val;
    Node *prev = nullptr;
    Node *next = nullptr;

    Node(const T &val) : val(val) {}
    template <typename... Args>
    Node(Args &&...args) : val(std::forward<Args>(args)...) {}
  };

  Node *head_ = nullptr;
  Node *tail_ = nullptr;
  size_t size_ = 0;

  using AllocTraits = std::allocator_traits<Allocator>;
  using NodeAllocator = typename AllocTraits::template rebind_alloc<Node>;
  using nodeAlloc = std::allocator_traits<NodeAllocator>;

  template <bool IsConst>
  class common_iterator;

  Allocator allocator_;
  NodeAllocator nodeallocator_;

 public:
  using iterator = common_iterator<false>;
  using const_iterator = common_iterator<true>;

  iterator begin() { return iterator(head_, this); }
  const_iterator begin() const { return const_iterator(head_, this); }
  const_iterator cbegin() const { return const_iterator(head_, this); }

  iterator end() {
    return iterator((tail_ == nullptr ? nullptr : tail_->next), this);
  }
  const_iterator end() const {
    return const_iterator((tail_ == nullptr ? nullptr : tail_->next), this);
  }
  const_iterator cend() const {
    return const_iterator((tail_ == nullptr ? nullptr : tail_->next), this);
  }

  List() : allocator_(), nodeallocator_(allocator_) {}
  List(const Allocator &alloc)
      : allocator_(AllocTraits::select_on_container_copy_construction(alloc)),
        nodeallocator_(allocator_) {}
  List(const List &other) : List(other.allocator_) {
    try {
      for (Node *curr = other.head_; curr != nullptr; curr = curr->next) {
        emplace(cend(), std::forward<T>(curr->val));
      }
    } catch (...) {
      Node *current = head_;
      while (current != nullptr) {
        Node *prv = current->next;
        nodeAlloc::destroy(nodeallocator_, current);
        nodeAlloc::deallocate(nodeallocator_, current, 1);
        current = prv;
      }
      head_ = nullptr;
      tail_ = nullptr;
      size_ = 0;
      throw;
    }
  }
  List(List &&other) :
        head_(other.head_),
        tail_(other.tail_),
        size_(other.size_), 
        allocator_(std::move(other.allocator_)),
        nodeallocator_(std::move(other.nodeallocator_)) {
    other.head_ = nullptr;
    other.tail_ = nullptr;
    other.size_ = 0;
  }

  void swap(List &other) {
    if constexpr (AllocTraits::propagate_on_container_swap::value) {
      std::swap(allocator_, other.allocator_);
    }
    if constexpr (nodeAlloc::propagate_on_container_swap::value) {
      std::swap(nodeallocator_, other.nodeallocator_);
    }
    std::swap(head_, other.head_);
    std::swap(tail_, other.tail_);
    std::swap(size_, other.size_);
  }

  List &operator=(const List &other) {
    if (this != &other) {
      if constexpr (AllocTraits::propagate_on_container_copy_assignment::value) {
        allocator_ = other.allocator_;
        nodeallocator_ = other.nodeallocator_;
      }
      List temp(other);
      swap(temp);
    }
    return *this;
  }

  ~List() {
    Node *current = head_;
    while (current != nullptr) {
      Node *next = current->next;
      nodeAlloc::destroy(nodeallocator_, current);
      nodeAlloc::deallocate(nodeallocator_, current, 1);
      current = next;
    }
    size_ = 0;
    head_ = nullptr;
    tail_ = nullptr;
  }

  Allocator get_allocator() const { return allocator_; }

  void clear() {
    Node *current = head_;
    while (current != tail_) {
      Node *next = current->next;
      nodeAlloc::destroy(nodeallocator_, current);
      nodeAlloc::deallocate(nodeallocator_, current, 1);
      current = next;
    }
    size_ = 0;
    head_ = tail_;
    tail_->prev = nullptr;
  }

  size_t size() const { return size_; }

  bool empty() { return size_ == 0; }

  template <typename... Args>
  iterator emplace(const_iterator pos, Args &&...args) {
    Node *pos_node = pos.node;
    Node *new_node = nodeAlloc::allocate(nodeallocator_, 1);
    try {
      nodeAlloc::construct(nodeallocator_, new_node, std::forward<Args>(args)...);
    } catch (...) {
      nodeAlloc::deallocate(nodeallocator_, new_node, 1);
      throw;
    }
    Node *prev_node = pos_node ? pos_node->prev : tail_;
    new_node->next = pos_node;
    new_node->prev = prev_node;
    if (pos_node) {
      pos_node->prev = new_node;
    } else {
      tail_ = new_node;
    }
    if (prev_node) {
      prev_node->next = new_node;
    } else {
      head_ = new_node;
    }
    ++size_;
    return iterator(new_node, this);
  }

  template <typename U>
  iterator insert(const_iterator pos, U &&val) {
    Node *pos_node = pos.node;
    Node *new_node = nodeAlloc::allocate(nodeallocator_, 1);
    try {
      nodeAlloc::construct(nodeallocator_, new_node, std::forward<U>(val));
    } catch (...) {
      nodeAlloc::deallocate(nodeallocator_, new_node, 1);
      throw;
    }
    Node *prev_node = pos_node->prev;
    new_node->next = pos_node;
    new_node->prev = prev_node;
    pos_node->prev = new_node;
    if (prev_node) {
      prev_node->next = new_node;
    } else {
      head_ = new_node;
    }

    ++size_;
    return iterator(new_node, this);
  }

  iterator erase(const_iterator pos) {
    Node *node_to_erase = pos.node;
    Node *prev_node = node_to_erase->prev;
    Node *next_node = node_to_erase->next;
    if (prev_node) {
      prev_node->next = next_node;
    } else {
      head_ = next_node;
    }
    if (next_node) {
      next_node->prev = prev_node;
    } else {
      tail_ = prev_node;
    }
    nodeAlloc::destroy(nodeallocator_, node_to_erase);
    nodeAlloc::deallocate(nodeallocator_, node_to_erase, 1);
    --size_;
    return iterator(next_node, this);
  }
};

template <typename T, typename Allocator>
template <bool IsConst>
class List<T, Allocator>::common_iterator {
 public:
  Node *node;
  const List *container;

  using value_type = std::conditional_t<IsConst, const T, T>;
  using pointer = value_type *;
  using difference_type = std::ptrdiff_t;
  using iterator_category = std::bidirectional_iterator_tag;
  using reference = value_type &;

  common_iterator() : node(nullptr), container(nullptr) {}

  common_iterator(Node *node, const List *container)
      : node(node), container(container) {}

  operator common_iterator<true>() const {
    return common_iterator<true>(node, container);
  }

  common_iterator &operator++() {
    if (node != nullptr) {
      node = node->next;
    }
    return *this;
  }

  common_iterator operator++(int) {
    common_iterator temp(*this);
    ++(*this);
    return temp;
  }

  common_iterator &operator--() {
    if (node == nullptr) {
      if (container == nullptr || container->tail_ == nullptr) {
        return *this;
      }
      node = container->tail_;
    } else {
      node = node->prev;
    }
    return *this;
  }

  template <bool OtherConst>
  bool operator==(const common_iterator<OtherConst> &other) const {
    return node == other.node;
  }

  template <bool OtherConst>
  bool operator!=(const common_iterator<OtherConst> &other) const {
    return !(*this == other);
  }

  common_iterator operator--(int) {
    common_iterator temp(*this);
    --(*this);
    return temp;
  }

  reference operator*() const { return node->val; }

  pointer operator->() const { return &(operator*()); }
};

template <typename Key, typename Value, typename Hash = std::hash<Key>,
          typename Equal = std::equal_to<Key>,
          typename Allocator = std::allocator<std::pair<const Key, Value> > >
class UnorderedMap {
 private:
  using NodeType = std::pair<Key, Value>;

  struct HashNode {
    NodeType map_node;
    size_t hash;

    HashNode(const NodeType &node, size_t h) : map_node(node), hash(h) {}
    HashNode(NodeType &&node, size_t h) : map_node(std::move(node)), hash(h) {}
  };

  template <bool IsConst>
  class common_iterator;

  double max_load_factor = 7.0 / 4.0;
  double load_factor() const { return (storage_.size()) / (buckets_.size()); }

  using AllocatorTraits = std::allocator_traits<Allocator>;
  using HashNodeAllocator = typename AllocatorTraits::template rebind_alloc<HashNode>;
  using HashNodeAllocatorTraits = std::allocator_traits<HashNodeAllocator>;
  using NodeTypeAllocator = typename AllocatorTraits::template rebind_alloc<NodeType>;
  using NodeAllocTraits = std::allocator_traits<NodeTypeAllocator>;
  using iter_storage = std::vector<typename List<HashNode, HashNodeAllocator>::iterator>;
  
  Equal equal;
  Hash hasher_;
  List<HashNode, HashNodeAllocator> storage_;
  iter_storage buckets_;
  HashNodeAllocator hashAlloc;
  Allocator Alloc;
  NodeTypeAllocator nodeAlloc;

 public:
  using key_type = Key;
  using mapped_type = Value;
  using size_type = size_t;
  using difference_type = std::ptrdiff_t;
  using hasher = Hash;
  using key_equal = Equal;
  using allocator_type = Allocator;
  using reference = NodeType &;
  using const_reference = const NodeType &;
  using iterator = common_iterator<false>;
  using const_iterator = common_iterator<true>;

  iterator begin() { return iterator(storage_.begin()); }
  const_iterator begin() const { return const_iterator(storage_.cbegin()); }
  const_iterator cbegin() const { return const_iterator(storage_.cbegin()); }

  iterator end() { return iterator(storage_.end()); }
  const_iterator end() const { return const_iterator(storage_.cend()); }
  const_iterator cend() const { return const_iterator(storage_.cend()); }

  UnorderedMap() : buckets_(10, storage_.end()) {}
  UnorderedMap(UnorderedMap &&other)
      : equal(std::move(other.equal)),
        hasher_(std::move(other.hasher_)),
        storage_(std::move(other.storage_)),
        buckets_(std::move(other.buckets_)),
        hashAlloc(std::move(other.hashAlloc)),
        Alloc(std::move(other.Alloc)),
        nodeAlloc(std::move(other.nodeAlloc)) {}
  UnorderedMap(const UnorderedMap &other)
      : equal(other.equal),
        hasher_(other.hasher_),
        storage_(other.storage_),
        buckets_(other.buckets_.size(), storage_.end()),
        hashAlloc(HashNodeAllocatorTraits::select_on_container_copy_construction(other.hashAlloc)),
        Alloc(AllocatorTraits::select_on_container_copy_construction(other.Alloc)),
        nodeAlloc(NodeAllocTraits::select_on_container_copy_construction(other.nodeAlloc)) {
    update_iterators(buckets_);
  }

  void update_iterators(iter_storage& bckt) {
    for (auto it = storage_.begin(); it != storage_.end(); ++it) {
      size_t b = it->hash % bckt.size();
      if (bckt[b] == storage_.end()) {
        bckt[b] = it;
      }
    }
  }

  ~UnorderedMap() = default;
  UnorderedMap &operator=(const UnorderedMap &other) {
    if (this != &other) {
      UnorderedMap newmap(other);
      swap(newmap);
    }
    return *this;
  }
  UnorderedMap &operator=(UnorderedMap &&other) {
    equal = std::move(other.equal);
    hasher_ = std::move(other.hasher_);
    if constexpr (AllocatorTraits::propagate_on_container_move_assignment::value) {
      Alloc = std::move(other.Alloc);
    }
    if constexpr (HashNodeAllocatorTraits::propagate_on_container_move_assignment::value) {
      hashAlloc = std::move(other.hashAlloc);
    }
    storage_ = std::move(other.storage_);
    buckets_.assign(other.bucketsz(), storage_.end());
    update_iterators(buckets_);
    return *this;
  }

  Value &operator[](Key &&key) {
    auto it = find(key);
    if (it != end()) {
      return it->second;
    } else {
      return emplace(std::move(key), Value()).first->second;
    }
  }

  Value &operator[](const Key &key) {
    auto it = find(key);
    if (it != end()) {
      return it->second;
    } else {
      return emplace(key, Value()).first->second;
    }
  }

  iterator find(const Key &key) {
    size_t hash = hasher_(key);
    size_t bucket = hash % bucketsz();
    auto it = buckets_[bucket];
    while (it != storage_.end() && hasher_(it->map_node.first) % bucketsz() == bucket) {
      if (it->hash == hash && equal(it->map_node.first, key)) {
        return iterator(it);
      }
      ++it;
    }
    return end();
  }

  void check() {
    if (load_factor() >= max_load_factor) {
      rehash(bucketsz() * 2);
    }
  }

  void rehash(size_t n) {
    size_t new_bucket_count = n;
    iter_storage new_buckets(new_bucket_count, storage_.end());
    update_iterators(new_buckets);
    buckets_.swap(new_buckets);
  }

  const Value &at(const Key &key) const {
    auto it = find(key);
    if (it == end()) {
      throw(std::out_of_range("No-no-no mister fish"));
    }
    return it->second;
  }

  Value &at(const Key &other) {
    auto it = find(other);
    if (it == end()) {
      throw(std::out_of_range("No-no-no mister fish"));
    }
    return it->second;
  }

  size_t size() const { return storage_.size(); }

  bool empty() const { return (size() == 0); }

  size_t bucketsz() const { return buckets_.size(); }

  template <typename U>
  std::pair<iterator, bool> insert_node(U &&node_value) {
    auto found = find(node_value.first);
    if (found != end()) {
      return {found, false};
    }
    check();
    size_t hash = hasher_(node_value.first);
    size_t bucket = hash % bucketsz();
    auto insert_pos = storage_.end();
    for (size_t i = bucket + 1; i < bucketsz(); ++i) {
      if (buckets_[i] != storage_.end()) {
        insert_pos = buckets_[i];
        break;
      }
    }
    auto list_it = storage_.emplace(insert_pos, std::forward<U>(node_value), hash);
    if (buckets_[bucket] == storage_.end()) {
      buckets_[bucket] = list_it;
    }
    return {iterator(list_it), true};
  }

  std::pair<iterator, bool> insert(const NodeType &v) { return insert_node(v); }

  std::pair<iterator, bool> insert(NodeType &&v) { return insert_node(std::move(v)); }

  template <typename... Args>
  std::pair<iterator, bool> emplace(Args &&...args) {
    NodeType *new_node = NodeAllocTraits::allocate(nodeAlloc, 1);
    NodeAllocTraits::construct(nodeAlloc, new_node, std::forward<Args>(args)...);
    auto it = insert(std::move(*new_node));
    check();
    return it;
  }

  template <typename InputIt>
  std::pair<iterator, bool> insert(InputIt first, InputIt last) {
    std::pair<iterator, bool> result{end(), false};
    for (auto it = first; it != last; ++it) {
      result = insert(std::forward<typename std::iterator_traits<InputIt>::reference>(*it));
    }
    check();
    return result;
  }

  iterator erase(const_iterator pos) {
    if (pos == cend()) {
      return end();
    }
    auto list_it = pos.iter;
    size_t idx = list_it->hash % bucketsz();
    if (buckets_[idx].node == list_it.node) {
      auto next_it = std::next(list_it);
      if (next_it != storage_.end() && next_it->hash % bucketsz() == idx) {
        buckets_[idx] = typename List<HashNode, HashNodeAllocator>::iterator(next_it.node, &storage_);
      } else {
        buckets_[idx] = storage_.end();
      }
    }
    auto next_list_it = storage_.erase(list_it);
    return iterator(next_list_it);
  }

  void erase(const_iterator first, const_iterator last) {
    while (first != last) {
      erase(first++);
    }
  }

  void reserve(size_t n) {
    size_t curr_load = (static_cast<size_t>((double(n) / max_load_factor)));
    if (++curr_load >= bucketsz()) {
      rehash(curr_load);
    }
  }

  void swap(const UnorderedMap &other) {
    std::swap(equal, other.equal);
    std::swap(hasher_, other.hasher_);
    std::swap(storage_, other.storage_);
    std::swap(buckets_, other.buckets_);
    if constexpr (HashNodeAllocatorTraits::propagate_on_container_swap::value) {
      std::swap(hashAlloc, other.hashAlloc);
    }
    if constexpr (AllocatorTraits::propagate_on_container_swap::value) {
      std::swap(Alloc, other.Alloc);
    }
  }
};

template <typename Key, typename Value, typename Hash, typename Equal,
          typename Allocator>
template <bool IsConst>
class UnorderedMap<Key, Value, Hash, Equal, Allocator>::common_iterator {
 public:
  using listiter = std::conditional_t<
      IsConst,
      typename List<HashNode,
                    HashNodeAllocator>::template common_iterator<true>,
      typename List<HashNode,
                    HashNodeAllocator>::template common_iterator<false> >;
  listiter iter;

  using value_type = NodeType;
  using reference = std::conditional_t<IsConst, const NodeType &, NodeType &>;
  using pointer = std::conditional_t<IsConst, const NodeType *, NodeType *>;
  using difference_type = std::ptrdiff_t;
  using iterator_category = std::forward_iterator_tag;

  common_iterator() = default;
  common_iterator(listiter it) : iter(it) {}

  operator common_iterator<true>() const { return common_iterator<true>(iter); }

  common_iterator &operator++() {
    ++iter;
    return *this;
  }

  common_iterator operator++(int) {
    common_iterator tmp(*this);
    ++(*this);
    return tmp;
  }

  reference operator*() const { return iter->map_node; }

  pointer operator->() const { return &(operator*()); }

  bool operator==(const common_iterator &other) const {
    return iter == other.iter;
  }

  bool operator!=(const common_iterator &other) const {
    return !(*this == other);
  }
};
