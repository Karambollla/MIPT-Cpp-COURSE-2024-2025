#include <iostream>
#include <memory>
#include <vector>
#include <concepts>
#include <type_traits>

template<typename T> class EnableSharedFromThis;
template<typename T> class WeakPtr;

template<typename T>
struct BaseControlBlock {
  size_t shared_count = 1;
  size_t weak_count = 1;
  virtual ~BaseControlBlock() = default;
  virtual void useDeleter(T* ptr) = 0;
  virtual void deallocate() = 0;
};

template <typename U, typename Allocator = std::allocator<U>, typename Deleter = std::default_delete<U>>
struct ControlBlockForRawPointer : BaseControlBlock<U> {

  using rebinded = typename std::allocator_traits<Allocator>::template rebind_alloc<ControlBlockForRawPointer<U, Allocator, Deleter>>;
  using trts = typename std::allocator_traits<rebinded>;

  U* value;
  Deleter deleter;
  rebinded allocator;

  void useDeleter(U*) override {
    deleter(value);
  }

  void deallocate() override {
    this->~ControlBlockForRawPointer();
    trts::deallocate(allocator, this, 1);
  }

  ControlBlockForRawPointer(U* ptr, Deleter deleterr) : value(ptr), deleter(deleterr) {
    this->shared_count = 1;
    this->weak_count = 1;
  }

};

template <typename U, typename Alloc = std::allocator<U>>
struct ControlBlockForMakeShared : BaseControlBlock<U> {
  
  using rebinded = typename std::allocator_traits<Alloc>::template rebind_alloc<ControlBlockForMakeShared<U, Alloc>>;
  using trts = typename std::allocator_traits<rebinded>;
  
  U value;
  Alloc alloc;
  rebinded allocator;

  void useDeleter(U*) override {
    std::allocator_traits<Alloc>::destroy(alloc, &(value));
  }
  
  template<typename... Args>
  ControlBlockForMakeShared(const Alloc& other, Args&&... args) : value(std::forward<Args>(args)...), alloc(other) {
    this->shared_count = 1;
    this->weak_count = 1;
  } 

  void deallocate() override {
    trts::deallocate(allocator, this, 1);
  }

  ControlBlockForMakeShared() = default;
}; 

template<typename T>
class SharedPtr {
  template<typename U> friend class WeakPtr;
  template <typename U> friend class SharedPtr;
public:

  SharedPtr() : ptr(nullptr), cb(nullptr) {}

  SharedPtr(const SharedPtr& other) : ptr(other.ptr), cb(other.cb) {
    plus();
  }

  template<typename Y>
  SharedPtr(const SharedPtr<Y>& other)
  requires std::convertible_to<Y*, T*>
  : ptr(static_cast<T*>(other.ptr)), cb(reinterpret_cast<BaseControlBlock<T>*>(other.cb)) {
    plus();
  }

  template<typename Y>
  SharedPtr(const SharedPtr<Y>& other, T* p)
  requires std::convertible_to<Y*, T*>
  : ptr(p), cb(reinterpret_cast<BaseControlBlock<T>*>(other.cb)) {
    plus();
  }
  
  SharedPtr(SharedPtr<T>&& other) : ptr(other.ptr), cb(other.cb) {
    other.ptr = nullptr;
    other.cb = nullptr;
  }

  template<typename Y>
  SharedPtr(SharedPtr<Y>&& other)
  requires std::convertible_to<Y*, T*>
  : ptr(other.ptr), cb(reinterpret_cast<BaseControlBlock<T>*>(other.cb)) {
    other.ptr = nullptr;
    other.cb = nullptr;
  }

  template<typename Y, typename Deleter>
  SharedPtr(Y* p, Deleter deleter)
  requires std::convertible_to<Y*, T*>
  {
    
    using RawCB = ControlBlockForRawPointer<Y, std::allocator<Y>, Deleter>;
    using Aaa = typename std::allocator_traits<std::allocator<Y>>::template rebind_alloc<RawCB>;
    using trts = std::allocator_traits<Aaa>;
    
    Aaa allocatorr;
    
    ptr = static_cast<T*>(p);
    if constexpr (std::is_base_of_v<EnableSharedFromThis<T>, T>) {
      ptr->wptr = *this;
    }
    auto k = trts::allocate(allocatorr, 1);
    cb = k;
    std::construct_at(k, RawCB(p, deleter));
  }
  template<typename Y, typename Deleter, typename Alloc>
  SharedPtr(Y* p, Deleter deleter, Alloc)
  requires std::convertible_to<Y*, T*>
  {
    using RawCB = ControlBlockForRawPointer<Y, Alloc, Deleter>;
    using Aaa = typename std::allocator_traits<Alloc>::template rebind_alloc<RawCB>;
    using trts = std::allocator_traits<Aaa>;
    
    Aaa allocatorr;

    ptr = static_cast<T*>(p);
    if constexpr (std::is_base_of_v<EnableSharedFromThis<T>, T>) {
      ptr->wptr = *this;
    }
    auto k = trts::allocate(allocatorr, 1);
    cb = k;
    std::construct_at(k, RawCB(p, deleter));
  }
  template<typename Y>
  SharedPtr(Y* p) 
  requires std::convertible_to<Y*, T*>
  {
    using RawCB = ControlBlockForRawPointer<Y>;
    using Aaa = typename std::allocator_traits<std::allocator<Y>>::template rebind_alloc<RawCB>;
    using Deleter = std::default_delete<Y>;
    using trts = std::allocator_traits<Aaa>;

    Aaa allocatorr;
    Deleter deleter;

    ptr = static_cast<T*>(p);
    if constexpr (std::is_base_of_v<EnableSharedFromThis<T>, T>) {
      ptr->wptr = *this;
    }

    auto k = trts::allocate(allocatorr, 1);
    cb = k;
    std::construct_at(k, RawCB(p, deleter));
  }

  void check() {
    if (cb) {
      --(cb->shared_count);
      if (cb->shared_count == 0) {
        cb->useDeleter(ptr);
        --(cb->weak_count);
        if (cb->weak_count == 0) {
          cb->deallocate();
        }
      }
    }
  }

  void plus() {
    if (cb) {
      ++cb->shared_count;
    }
  }

  SharedPtr<T>& operator=(SharedPtr<T>&& other) {
    SharedPtr<T> temp(std::move(other));
    swap(temp);
    return *this;
  }
  template<typename Y>
  SharedPtr<T>& operator=(SharedPtr<Y>&& other)
  requires std::convertible_to<Y*, T*>
  {
    SharedPtr<T> temp(std::move(other));
    swap(temp);
    return *this;
  } 

  SharedPtr<T>& operator=(const SharedPtr<T>& other) {
    if (&other != this) {
      SharedPtr<T> temp(other);
      swap(temp);
    }
    return *this;
  }
  template<typename Y>
  SharedPtr<T>& operator=(const SharedPtr<Y>& other)
  requires std::convertible_to<Y*, T*>
  {
    SharedPtr<T> temp(other);
    swap(temp);
    return *this;
  } 
  
  ~SharedPtr() {
    check();
  }

  int use_count() const {
    return cb->shared_count;
  }

  void reset() {
    SharedPtr temp;
    swap(temp);
  }

  template<typename Y>
  void reset(Y* ptr)
  requires std::convertible_to<Y*, T*>
  {
    SharedPtr<Y> temp(ptr);
    swap(temp);
  }

  void swap(SharedPtr<T>& other) {
    std::swap(ptr, other.ptr);
    std::swap(cb, other.cb);
  }

  T* get() const {
    return ptr;
  }

  T& operator*() const {
    return *ptr;
  }

  T* operator->() const {
    return ptr;
  }

private:

  SharedPtr(T* p, BaseControlBlock<T>* cb_) : ptr(p), cb(cb_) {
    plus();
  }

  template <typename Allocator>
  SharedPtr(ControlBlockForMakeShared<T, Allocator>* cb_ptr) : ptr(&(cb_ptr->value)), cb(cb_ptr) {}
  
  T* ptr;
  BaseControlBlock<T>* cb;
  
  template<typename U, typename Alloc, typename... Args>
  friend SharedPtr<U> allocateShared(const Alloc& alloc, Args&&... args);
  
  template<typename Y, typename... Args>
  friend SharedPtr<Y> makeShared(Args&&... args);
};

template<typename U, typename Alloc, typename... Args>
SharedPtr<U> allocateShared(const Alloc& alloc, Args&&... args) {
  using rebind = typename std::allocator_traits<Alloc>::template rebind_alloc<ControlBlockForMakeShared<U, Alloc>>;
  rebind cb_alloc;
  auto cb = std::allocator_traits<rebind>::allocate(cb_alloc, 1);
  std::allocator_traits<rebind>::construct(cb_alloc, cb, alloc, std::forward<Args>(args)...);
  return SharedPtr<U>(cb);
}

template<typename T, typename... Args>
SharedPtr<T> makeShared(Args&&... args) {
  using Alloc = std::allocator<T>;
  auto cb = new ControlBlockForMakeShared<T, Alloc>(Alloc{}, std::forward<Args>(args)...);
  return SharedPtr<T>(cb);
}

template<typename T>
class WeakPtr {
  template<typename U> friend class SharedPtr;
  template<typename U> friend class WeakPtr;
private:
  
  T* ptr;
  BaseControlBlock<T>* cb;

public:
  
  WeakPtr() : ptr(nullptr), cb(nullptr) {}

  template<typename U>
  WeakPtr(const SharedPtr<U>& other)
  requires std::convertible_to<U*, T*>
  : ptr(static_cast<T*>(other.ptr)), cb(reinterpret_cast<BaseControlBlock<T>*>(other.cb)) {
    if (cb) {
      ++(cb->weak_count);
    }
  }

  WeakPtr(const WeakPtr<T>& other) : ptr(other.ptr), cb(other.cb) {
    if (cb) {
      ++(cb->weak_count);
    }
  }
  template<typename U>
  WeakPtr(const WeakPtr<U>& other)
  requires std::convertible_to<U*, T*>
  : ptr(static_cast<T*>(other.ptr)), cb(reinterpret_cast<BaseControlBlock<T>*>(other.cb)) {
    if (cb) {
      ++(cb->weak_count);
    }
  }

  WeakPtr(WeakPtr<T>&& other) : ptr(other.ptr), cb(other.cb) {
    other.ptr = nullptr;
    other.cb = nullptr;
  }
  template<typename U>
  WeakPtr(WeakPtr<U>&& other)
  requires std::convertible_to<U*, T*>
  : ptr(static_cast<T*>(other.ptr)), cb(reinterpret_cast<BaseControlBlock<T>*>(other.cb)) {
    other.ptr = nullptr;
    other.cb = nullptr;
  }

  void swap(WeakPtr& other) {
    std::swap(ptr, other.ptr);
    std::swap(cb, other.cb);
  }

  WeakPtr<T>& operator=(const WeakPtr<T>& other) {
    if (this != &other) {
      WeakPtr<T> temp(other);
      swap(temp);
    }
    return *this;
  }
  template<typename U>
  WeakPtr<T>& operator=(const WeakPtr<U>& other)
  requires std::convertible_to<U*, T*>
  {
    if (this != &other) {
      WeakPtr<T> temp(other);
      swap(temp);
    }
    return *this;
  }

  WeakPtr<T>& operator=(WeakPtr<T>&& other) {
    WeakPtr<T> temp(std::move(other));
    swap(temp);
    return *this;
  }
  template<typename U>
  WeakPtr<T>& operator=(WeakPtr<U>&& other)
  requires std::convertible_to<U*, T*>
  {
    WeakPtr<T> temp(std::move(other));
    swap(temp);
    return *this;
  }
  
  template<typename U>
  WeakPtr<T>& operator=(const SharedPtr<U>& shared) 
  requires std::convertible_to<U*, T*>
  {
    WeakPtr<T> temp(shared);
    swap(temp);
    return *this;
  }

  size_t use_count() const {
    return cb->shared_count;
  }

  void check() {
    if (cb) {
      (--cb->weak_count);
      if ((cb->weak_count) == 0) {
        if (cb->shared_count == 0) {
          cb->deallocate();
        }
      }
    }
  }

  ~WeakPtr() {
    check();
  }

  bool expired() const {
    if (cb) {
      return cb->shared_count == 0;
    }
    return !cb;
  }

  SharedPtr<T> lock() const {
    if (!cb || cb->shared_count == 0) {
      return SharedPtr<T>();
    }
    return SharedPtr<T>(ptr, cb);
  }

};

template<typename T>
class EnableSharedFromThis {
  template<typename U>
  friend class SharedPtr;
private:

  WeakPtr<T> wptr;

public:

  SharedPtr<T> shared_from_this() const {
    return wptr.lock();
  }

};