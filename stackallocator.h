#include <cstddef>
#include <new>
#include <memory>
#include <iterator>
#include <stdexcept>
#include <type_traits>
#include <iostream>

template<size_t N>
struct StackStorage {
  size_t header = 0;
  std::byte buffer[N];

  StackStorage() = default;
  StackStorage(const StackStorage<N>&) = delete;

  ~StackStorage() = default;
  StackStorage& operator=(const StackStorage<N>& other) = delete;

  void* allocate(size_t n, size_t align) {
    void* aligned_ptr = buffer + header;
    size_t remain = N - header;
    if (std::align(align, n, aligned_ptr, remain) == nullptr) {
      throw std::bad_alloc();
    }
    std::byte* result_ptr = static_cast<std::byte*>(aligned_ptr);
    header = result_ptr - buffer + n;
    return result_ptr;
  }
};

template<typename T, size_t N>
class StackAllocator {
  template<typename, size_t>
  friend class StackAllocator;
private:
  StackStorage<N>& storage;

public:
  using pointer = T*;
  using value_type = T;
  using const_pointer = const T*;
  using size_type = size_t;
  using difference_type = std::ptrdiff_t;

  StackAllocator() = delete;
  StackAllocator(StackStorage<N>& other) : storage(other) {}

  template<typename U>
  StackAllocator(const StackAllocator<U, N>& other)
    : storage(other.storage) {}
  ~StackAllocator() = default;
  StackAllocator& operator=(const StackAllocator& other) = default;

  StackAllocator select_on_container_copy_construction() {
    return *this;
  }

  pointer allocate(size_t n) {
    void* a = storage.allocate(n * sizeof(T), alignof(T));
    return reinterpret_cast<pointer>(a);
  }
  void deallocate(pointer, size_t) { }

  bool operator==(const StackAllocator& other) const {
    return &storage == &other.storage;
  }
  bool operator!=(const StackAllocator& other) const {
    return !(*this == other);
  }

  template<typename U>
  struct rebind {
    using other = StackAllocator<U, N>;
  };
};

template<typename T, typename Allocator = std::allocator<T>>
class List {
private:

  struct Node {
    T val;
    Node* prev = nullptr;
    Node* next = nullptr;

    Node() : val(T()) {}
    Node(const T& val) : val(val) {}
  };

  Node* head_ = nullptr;
  Node* tail_ = nullptr;
  size_t size_ = 0;

  using AllocTraits = std::allocator_traits<Allocator>;
  using NodeAllocator = typename AllocTraits::template rebind_alloc<Node>;
  using nodeAlloc = std::allocator_traits<NodeAllocator>;

  template<bool isConst>
  class common_iterator;

  [[no_unique_address]] Allocator allocator_;
  [[no_unique_address]] NodeAllocator nodeallocator_;

public:

  using iterator = common_iterator<false>;
  using const_iterator = common_iterator<true>;

  using reverse_iterator = std::reverse_iterator<iterator>;
  using const_reverse_iterator = std::reverse_iterator<const_iterator>;
  //
  iterator begin() { return iterator(head_, this); }
  const_iterator begin() const { return const_iterator(head_, this); }
  const_iterator cbegin() const { return const_iterator(head_, this); }

  iterator end() { return iterator(tail_->next, this); }
  const_iterator end() const { return const_iterator(tail_->next, this); }
  const_iterator cend() const { return const_iterator(tail_->next, this); }

  reverse_iterator rbegin() { return std::make_reverse_iterator(end()); }
  const_reverse_iterator rbegin() const { return std::make_reverse_iterator(end()); }
  const_reverse_iterator crbegin() const { return std::make_reverse_iterator(cend()); }

  reverse_iterator rend() { return std::make_reverse_iterator(begin()); }
  const_reverse_iterator rend() const { return std::make_reverse_iterator(begin()); }
  const_reverse_iterator crend() const { return std::make_reverse_iterator(cbegin()); }

  List() : allocator_(), nodeallocator_(allocator_) {}
  List(const Allocator& alloc) : allocator_(AllocTraits::select_on_container_copy_construction(alloc)), nodeallocator_(allocator_) {}

  void clear() {
    Node* current = head_;
    while (current != nullptr) {
      Node* next_node = current->next;
      nodeAlloc::destroy(nodeallocator_, current);
      nodeAlloc::deallocate(nodeallocator_, current, 1);
      current = next_node;
    }
    head_ = nullptr;
    tail_ = nullptr;
    size_ = 0;
  }

  List(size_t n) : List() {
    try {
      for (size_t i = 0; i < n; ++i) {
        push_back();
      }
    }
    catch (...) {
      clear();
      throw;
    }
  }

  List(size_t n, const T& other) : List() {
    try {
      for (size_t i = 0; i < n; ++i) {
        push_back(other);
      }
    }
    catch (...) {
      clear();
      throw;
    }
  }

  List(size_t n, const Allocator& allocator) : List(allocator) {
    try {
      for (size_t i = 0; i < n; ++i) {
        push_back();
      }
    }
    catch (...) {
      clear();
      throw;
    }
  }

  List(size_t n, const T& other, const Allocator& allocator) : List(allocator) {
    try {
      for (size_t i = 0; i < n; ++i) {
        push_back(other);
      }
    }
    catch (...) {
      clear();
      throw;
    }
  }

  List(const List& other) : List(other.allocator_) {
    try {
      for (Node* curr = other.head_; curr != nullptr; curr = curr->next) {
        push_back(curr->val);
      }
    }
    catch (...) {
      clear();
      throw;
    }
  }

  void swap(List& other) {
    std::swap(head_, other.head_);
    std::swap(tail_, other.tail_);
    std::swap(size_, other.size_);
  }

  List& operator=(const List& other) {
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
    clear();
  }

  Allocator get_allocator() const {
    return allocator_;
  }

  size_t size() const {
    return size_;
  }

  void push_back() {
    Node* newval = nodeAlloc::allocate(nodeallocator_, 1);
    try {
      nodeAlloc::construct(nodeallocator_, newval);
    }
    catch (...) {
      nodeAlloc::deallocate(nodeallocator_, newval, 1);
      throw;
    }
    newval->next = nullptr;
    newval->prev = nullptr;
    if (size_ == 0) {
      head_ = newval;
      tail_ = newval;
    }
    else {
      tail_->next = newval;
      newval->prev = tail_;
      tail_ = newval;
    }
    ++size_;
  }

  void push_back(const T& other) {
    Node* newval = nodeAlloc::allocate(nodeallocator_, 1);
    try {
      nodeAlloc::construct(nodeallocator_, newval, other);
    }
    catch (...) {
      nodeAlloc::deallocate(nodeallocator_, newval, 1);
      throw;
    }
    newval->next = nullptr;
    newval->prev = nullptr;
    if (size_ == 0) {
      head_ = newval;
      tail_ = newval;
    }
    else {
      tail_->next = newval;
      newval->prev = tail_;
      tail_ = newval;
    }
    ++size_;
  }

  void push_front(const T& other) {
    Node* newval = nodeAlloc::allocate(nodeallocator_, 1);
    try {
      nodeAlloc::construct(nodeallocator_, newval, other);
    }
    catch (...) {
      nodeAlloc::deallocate(nodeallocator_, newval, 1);
      throw;
    }
    newval->prev = nullptr;
    newval->next = nullptr;

    if (size_ == 0) {
      head_ = newval;
      tail_ = newval;
    }
    else {
      newval->next = head_;
      head_->prev = newval;
      head_ = newval;
    }
    ++size_;
  }

  void pop_back() {
    if (!empty()) {
      Node* prv = tail_->prev;
      nodeAlloc::destroy(nodeallocator_, tail_);
      nodeAlloc::deallocate(nodeallocator_, tail_, 1);
      --size_;
      if (size() == 0) {
        tail_ = nullptr;
        head_ = nullptr;
      }
      else {
        tail_ = prv;
        tail_->next = nullptr;
      }
    }
  }

  void pop_front() {
    if (!empty()) {
      Node* prv = head_->next;
      nodeAlloc::destroy(nodeallocator_, head_);
      nodeAlloc::deallocate(nodeallocator_, head_, 1);
      --size_;
      if (size() == 0) {
        tail_ = nullptr;
        head_ = nullptr;
      }
      else {
        head_ = prv;
        head_->prev = nullptr;
      }
    }
  }

  bool empty() {
    return size_ == 0;
  }

  iterator insert(const_iterator other, const T& val) {
    if (other.node == nullptr) {
      push_back(val);
      return iterator(tail_, this);
    }
    if (other.node == head_) {
      push_front(val);
      return iterator(head_, this);
    }
    Node* newnode = nodeAlloc::allocate(nodeallocator_, 1);
    try {
      nodeAlloc::construct(nodeallocator_, newnode, val);
    }
    catch (...) {
      nodeAlloc::deallocate(nodeallocator_, newnode, 1);
      throw;
    }
    Node* nodeprev = other.node->prev;
    newnode->next = other.node;
    newnode->prev = nodeprev;
    nodeprev->next = newnode;
    other.node->prev = newnode;
    ++size_;
    return iterator(newnode, this);
  }

  iterator erase(const_iterator other) {
    if (head_ == other.node) {
      pop_front();
      return iterator(head_, this);
    }
    if (tail_ == other.node) {
      pop_back();
      return iterator(tail_, this);
    }
    Node* nodeprev = other.node->prev;
    Node* nodenext = other.node->next;
    nodeprev->next = nodenext;
    nodenext->prev = nodeprev;
    nodeAlloc::deallocate(nodeallocator_, other.node, 1);
    --size_;
    return iterator(nodenext, this);
  }
};

template<typename T, typename Allocator>
template <bool IsConst>
class List<T, Allocator>::common_iterator {
public:
  Node* node;
  const List* container;

  using value_type = std::conditional_t<IsConst, const T, T>;
  using pointer = value_type*;
  using difference_type = std::ptrdiff_t;
  using iterator_category = std::bidirectional_iterator_tag;
  using reference = value_type&;

  common_iterator(Node* node, const List* container)
    : node(node), container(container) {}

  operator common_iterator<true>() const {
    return common_iterator<true>(node, container);
  }

  common_iterator& operator++() {
    if (node != nullptr)
      node = node->next;
    return *this;
  }

  common_iterator operator++(int) {
    common_iterator temp(*this);
    ++(*this);
    return temp;
  }

  common_iterator& operator--() {
    if (node == nullptr) {
      if (container == nullptr || container->tail_ == nullptr) {
        return *this;
      }
      node = container->tail_;
    }
    else {
      node = node->prev;
    }
    return *this;
  }

  common_iterator operator--(int) {
    common_iterator temp(*this);
    --(*this);
    return temp;
  }

  bool operator==(const common_iterator& other) const {
    return node == other.node;
  }

  bool operator!=(const common_iterator& other) const {
    return !(*this == other);
  }

  reference operator*() const {
    return node->val;
  }

  pointer operator->() const {
    return &(operator*());
  }
};