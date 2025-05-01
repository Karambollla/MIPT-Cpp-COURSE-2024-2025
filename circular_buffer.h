#include <limits>
#include <type_traits>
#include <array>
#include <stdexcept>
#include <optional>
#include <memory>
#include <iostream>
#include <exception>
#include <utility>
#include <cstddef>
#include <algorithm>
#include <new>

constexpr size_t DYNAMIC_CAPACITY = std::numeric_limits<std::size_t>::max();

template<typename T, size_t cap, bool IsStatic>
struct dummy {
  size_t capacity_meter_;
  dummy(size_t capac) : capacity_meter_(capac) {}
  constexpr size_t getcap() const { return capacity_meter_; }
};

template<typename T, size_t cap>
struct dummy<T, cap, true> {
  dummy(size_t) {}
  size_t getcap() const { return cap; }
};

template<typename T, size_t cap = DYNAMIC_CAPACITY>
class CircularBuffer {
private:
  static constexpr bool is_static = (cap != DYNAMIC_CAPACITY);

  struct StaticStorage {
    alignas(T) char buffer_[(is_static ? cap : 1) * sizeof(T)];
  };
  struct DynamicStorage {
    T* buffer_ = nullptr;
  };

  std::conditional_t<is_static, StaticStorage, DynamicStorage> buffer_;

  size_t capacity_;
  size_t head_;
  size_t tail_;
  size_t sz_;

  template<bool isStatic>
  using capacitymeter = dummy<T, cap, isStatic>;

  template<bool isConst>
  class common_iterator;

public:
  using iterator = common_iterator<false>;
  using const_iterator = common_iterator<true>;

  using reverse_iterator = std::reverse_iterator<iterator>;
  using const_reverse_iterator = std::reverse_iterator<const_iterator>;

  iterator begin() { return iterator(data(), head_, 0, capacity_); }
  const_iterator begin() const { return const_iterator(data(), head_, 0, capacity_); }
  const_iterator cbegin() const { return const_iterator(data(), head_, 0, capacity_); }

  iterator end() { return iterator(data(), head_, size(), capacity_); }
  const_iterator end() const { return const_iterator(data(), head_, size(), capacity_); }
  const_iterator cend() const { return const_iterator(data(), head_, size(), capacity_); }

  reverse_iterator rbegin() { return std::make_reverse_iterator(end()); }
  const_reverse_iterator rbegin() const { return std::make_reverse_iterator(end()); }
  const_reverse_iterator crbegin() const { return std::make_reverse_iterator(cend()); }

  reverse_iterator rend() { return std::make_reverse_iterator(begin()); }
  const_reverse_iterator rend() const { return std::make_reverse_iterator(begin()); }
  const_reverse_iterator crend() const { return std::make_reverse_iterator(cbegin()); }


  T* data() {
    if constexpr (is_static) {
      return reinterpret_cast<T*>(buffer_.buffer_);
    }
    else {
      return buffer_.buffer_;
    }
  }

  const T* data() const {
    if constexpr (is_static) {
      return reinterpret_cast<const T*>(buffer_.buffer_);
    }
    else {
      return buffer_.buffer_;
    }
  }

  size_t capacity() const { return capacity_; }
  size_t size() const { return sz_; }
  bool empty() const { return sz_ == 0; }
  bool full() const { return sz_ == capacity_; }

  T& operator[](size_t i) {
    return data()[(head_ + i) % capacity_];
  }

  const T& operator[](size_t i) const {
    return data()[(head_ + i) % capacity_];
  }

  T& at(size_t i) {
    if (i >= size()) {
      throw std::out_of_range("outofrange");
    }
    return (*this)[i];
  }

  CircularBuffer() : capacity_(cap), head_(0), tail_(0), sz_(0) {}

  explicit CircularBuffer(std::size_t capacity_arg) : head_(0), tail_(0), sz_(0) {
    if constexpr (is_static) {
      if (capacity_arg != cap) {
        throw std::invalid_argument("Invalid cap");
      }
      capacity_ = capacity_arg;
    }
    else {
      capacity_ = capacity_arg;
      buffer_.buffer_ = reinterpret_cast<T*>(::operator new[](capacity_ * sizeof(T), std::align_val_t{ alignof(T) }));
    }
  }

  CircularBuffer(const CircularBuffer& z) : capacity_(z.capacity_), head_(z.head_), tail_(z.tail_), sz_(z.sz_) {
    if constexpr (is_static) {
      for (size_t i = 0; i < sz_; ++i) {
        std::construct_at(data() + i, T(z.data()[i]));
      }
    }
    else {
      buffer_.buffer_ = reinterpret_cast<T*>(::operator new[](capacity_ * sizeof(T), std::align_val_t{ alignof(T) }));
      size_t k = 0;
      try {
        for (size_t i = 0; i < z.sz_; ++i) {
          std::construct_at(buffer_.buffer_ + i, T(z.data()[i]));
          ++k;
        }
      }
      catch (...) {
        for (size_t j = 0; j < k; ++j) {
          std::destroy_at(buffer_.buffer_ + j);
        }
        ::operator delete[](reinterpret_cast<void*>(buffer_.buffer_), std::align_val_t{ alignof(T) });
        throw;
      }
    }
  }

  CircularBuffer& operator=(const CircularBuffer& z) {
    if (this == &z) { return *this; }
    if constexpr (!is_static) {
      CircularBuffer temp(z);
      swap(temp);
    }
    else {
      for (size_t i = 0; i < sz_; ++i) {
        std::destroy_at(data() + (head_ + i) % capacity_);
      }
      head_ = z.head_;
      tail_ = z.tail_;
      sz_ = z.sz_;
      size_t k = 0;
      try {
        for (size_t i = 0; i < sz_; ++i) {
          std::construct_at(data() + (head_ + i) % capacity_, T(z.data()[(z.head_ + i) % z.capacity_]));
          k++;
        }
      }
      catch (...) {
        for (size_t i = 0; i < k; ++i) {
          std::destroy_at(data() + (head_ + i) % capacity_);
        }
        head_ = 0;
        tail_ = 0;
        sz_ = 0;
        throw;
      }
    }
    return *this;
  }

  ~CircularBuffer() {
    for (size_t i = 0; i < sz_; ++i) {
      std::destroy_at(data() + (head_ + i) % capacity_);
    }
    if constexpr (!is_static) {
      ::operator delete[](reinterpret_cast<void*>(data()), std::align_val_t{ alignof(T) });
    }
  }

  void push_back(const T& other) {
    T prev(other);
    if (full()) {
      std::destroy_at(data() + head_);
      head_ = (head_ + 1) % capacity_;
    }
    else {
      ++sz_;
    }
    std::construct_at((data() + tail_), T(prev));
    tail_ = (tail_ + 1) % capacity_;
  }

  void push_front(const T& other) {
    T prev(other);
    if (full()) {
      tail_ = (tail_ - 1 + capacity_) % capacity_;
      std::destroy_at(data() + tail_);
    }
    else {
      ++sz_;
    }
    head_ = (head_ - 1 + capacity_) % capacity_;
    std::construct_at(data() + head_, T(prev));
  }

  void pop_back() {
    if (!empty()) {
      tail_ = (tail_ - 1 + capacity_) % capacity_;
      std::destroy_at(data() + tail_);
      --sz_;
    }
  }

  void pop_front() {
    if (!empty()) {
      std::destroy_at(data() + head_);
      head_ = (head_ + 1) % capacity_;
      --sz_;
    }
  }

  void insert(iterator pos, const T& value) {
    size_t current_cap = capacity_;

    size_t pos_i = pos.rel_pos();
    bool mefull = full();
    size_t N = size();

    if (mefull && pos_i == 0) {
      return;
    }

    if (mefull) {
      std::destroy_at(data() + head_);
      head_ = (head_ + 1) % current_cap;
      N = current_cap - 1;
      if (pos_i > 0) {
        --pos_i;
      }
      else {
        pos_i = 0;
      }
    }
    else {
      ++sz_;
      tail_ = (tail_ + 1) % current_cap;
    }

    for (size_t k = N; k > pos_i; --k) {
      size_t dest_idx = (head_ + k) % current_cap;
      size_t src_idx = (head_ + k - 1) % current_cap;
      std::construct_at(data() + dest_idx, T(data()[src_idx]));
      std::destroy_at(data() + src_idx);
    }
    std::construct_at(data() + (head_ + pos_i) % current_cap, T(value));
  }

  void erase(iterator pos) {
    size_t N = size();
    size_t pos_i = pos.rel_pos();

    if (empty()) {
      return;
    }
    --sz_;
    for (size_t k = pos_i; k < N - 1; ++k) {
      data()[(head_ + k) % capacity_] = std::move(data()[(head_ + k + 1) % capacity_]);
    }
    std::destroy_at(&data()[(head_ + N - 1) % capacity_]);
    tail_ = (tail_ + capacity_ - 1) % capacity_;
  }

  void swap(CircularBuffer& other) {
    if (this == &other) {
      return;
    }
    std::swap(head_, other.head_);
    std::swap(tail_, other.tail_);
    std::swap(sz_, other.sz_);
    std::swap(capacity_, other.capacity_);
    std::swap(buffer_, other.buffer_);
  }

  friend iterator operator+(typename iterator::difference_type n, const iterator& other) {
    return other + n;
  }
};

template<typename T, size_t cap>
template <bool IsConst>
class CircularBuffer<T, cap>::common_iterator : public CircularBuffer<T, cap>::template capacitymeter<CircularBuffer<T, cap>::is_static> {
public:

  using value_type = T;
  using pointer = std::conditional_t<IsConst, const T*, T*>;
  using reference = std::conditional_t<IsConst, const T&, T&>;
  using difference_type = std::ptrdiff_t;
  using iterator_category = std::random_access_iterator_tag;
  using iter_cap = capacitymeter<is_static>;

  common_iterator(pointer ptr, size_t head, size_t rel_head, size_t capacity)
    : iter_cap(capacity), ptr_(ptr), head_(head), rel_head_(rel_head) {}

  operator common_iterator<true>() const {
    return common_iterator<true>(ptr_, head_, rel_head_, conditional_getcap());
  }

  common_iterator& operator++() {
    ++rel_head_;
    return *this;
  }

  common_iterator operator++(int) {
    common_iterator temp(*this);
    ++(*this);
    return temp;
  }

  common_iterator& operator--() {
    --rel_head_;
    return *this;
  }

  common_iterator operator--(int) {
    common_iterator temp(*this);
    --(*this);
    return temp;
  }

  common_iterator& operator+=(difference_type n) {
    rel_head_ += n;
    return *this;
  }

  common_iterator operator+(difference_type n) const {
    common_iterator temp(*this);
    temp += n;
    return temp;
  }

  common_iterator& operator-=(difference_type n) {
    rel_head_ -= n;
    return *this;
  }

  common_iterator operator-(difference_type n) const {
    common_iterator temp(*this);
    temp -= n;
    return temp;
  }

  difference_type operator-(const common_iterator& other) const {
    return static_cast<difference_type>(rel_head_) - static_cast<difference_type>(other.rel_head_);
  }

  reference operator*() const {
    return ptr_[(head_ + rel_head_) % conditional_getcap()];
  }

  pointer operator->() const {
    return &ptr_[(head_ + rel_head_) % conditional_getcap()];
  }

  bool operator<(const common_iterator& other) const { return rel_head_ < other.rel_head_; }
  bool operator>(const common_iterator& other) const { return rel_head_ > other.rel_head_; }
  bool operator<=(const common_iterator& other) const { return rel_head_ <= other.rel_head_; }
  bool operator>=(const common_iterator& other) const { return rel_head_ >= other.rel_head_; }

  bool operator==(const common_iterator& other) const {
    return (ptr_ == other.ptr_) && (head_ == other.head_) && (rel_head_ == other.rel_head_);
  }

  bool operator!=(const common_iterator& other) const {
    return !(*this == other);
  }

  size_t rel_pos() const {
    return rel_head_;
  }

private:

  size_t conditional_getcap() const {
    if constexpr (is_static) {
      return cap;
    }
    return this->getcap();
  }

  pointer ptr_;
  size_t head_;
  size_t rel_head_;

};
