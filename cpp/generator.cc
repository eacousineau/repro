#include <cassert>
#include <iostream>
#include <functional>
#include <experimental/optional>
#include <memory>
#include <vector>

using std::cerr;
using std::endl;
using std::experimental::optional;
using std::unique_ptr;

template <typename T, typename SFINAE = void>
struct generator_value_type {
  using value_type = typename T::value_type;
};

template <typename T>
using result_of_t = decltype(std::declval<T>()());

/**
Provides a make_generator class that can be iterated upon:
@tparam T
  Type to return when iterating
@tparam result_t (default: optional<T>)
  Class that must follow this contract:
   - moveable
   - (optional) copy constructible
   - result_t() -> construct invalid value
   - result_t(T&&) -> construct valid value
   - operator* -> T&&
      dereference to value, moveable
   - operator bool():
      true -> valid value, consume
      false -> invalid value, stop iterating
Imitates Python make_generator expressions:
- begin() returns an iterator that has evaluated the first expression
- end() refers to the end of iteration (no more values to consume)
- Can only be iterated once; afterwards, it will always return
  `begin() == end()`.
 */
template <typename Func>
class generator_t {
 public:
  using result_type = result_of_t<Func>;
  using value_type = typename generator_value_type<result_type>::value_type;

  generator_t(Func&& func)
      : func_(std::forward<Func>(func)) {}

  template <typename OtherFunc>
  generator_t(generator_t<OtherFunc>&& other)
      : func_(std::move(other.func_)),
        value_(std::move(other.value_)) {}

  class iterator {
   public:
    iterator() {}
    iterator(generator_t* parent, bool is_end) : parent_(parent) {
      assert(valid());
      if (!is_end) {
        ++(*this);
      } else {
        count_ = -1;
      }
    }
    bool valid() const {
      return parent_ != nullptr;
    }
    value_type&& operator*() {
      assert(valid());
      return std::forward<value_type>(*parent_->value_);
    }
    void operator++() {
      assert(valid());
      ++count_;
      if (!parent_->store_next()) {
        count_ = -1;
      }
    }
    bool operator==(const iterator& other) {
      return parent_ == other.parent_ && count_ == other.count_;
    }
    bool operator!=(const iterator& other) { return !(*this == other); }
   private:
    generator_t* parent_{};
    int count_{};
  };

  iterator begin() { return iterator(this, false); }
  iterator end() { return iterator(this, true); }

  // May invalidate iterator state; can restore iterator state with `++iter`.
  result_type next() {
    store_next();
    return std::move(value_);
  }
 private:
  friend class iterator;
  template <typename OtherFunc>
  friend class generator_t;
  bool store_next() {
    value_ = func_();
    return bool{value_};
  }
  Func func_;
  result_type value_;
};

template <typename result_type>
using function_generator = generator_t<std::function<result_type()>>;

template <typename Func>
auto make_generator(Func&& func) {
  return generator_t<Func>(std::forward<Func>(func));
}

template <typename result_t>
auto null_generator() {
  return make_generator([]() { return result_t{}; });
}

template <typename Generator>
class chain_func {
 public:
  using result_type = typename Generator::result_type;

  chain_func(std::vector<Generator> g_list)
      : g_list_(std::move(g_list)) {}

  result_type operator()() {
    if (!next()) return {};
    return *iter_;
  }

 private:
  inline Generator& g() { return g_list_[i_]; }

  inline bool next() {
    if (i_ >= g_list_.size()) return false;
    if (!iter_.valid()) {
      iter_ = g().begin();
    } else {
      ++iter_;
    }
    while (iter_ == g().end()) {
      if (++i_ >= g_list_.size()) return false;
      iter_ = g().begin();
    }
    return true;
  }

  std::vector<Generator> g_list_;
  typename Generator::iterator iter_;
  int i_{0};
};

template <typename Generator>
auto make_chain_list_raw(std::vector<Generator> g) {
  auto compose = chain_func<Generator>(std::move(g));
  return make_generator<decltype(compose)>(std::move(compose));
}

template <typename First, typename ... Remaining>
auto make_chain(First&& first, Remaining&&... remaining) {
  return make_chain_list_raw<First>({
      std::forward<First>(first), std::forward<Remaining>(remaining)...});
}

template <typename result_type>
auto make_chain_list(std::vector<function_generator<result_type>> list) {
  return make_chain_list_raw<function_generator<result_type>>(std::move(list));
}

template <typename T>
std::ostream& operator<<(std::ostream& os, const unique_ptr<T>& p) {
  if (p) os << "unique_ptr(" << *p << ")";
  else os << "unique_ptr(nullptr)";
  return os;
}

template <typename Container>
void print_container(Container&& gen) {
  for (int rep : {0, 1}) {
    cerr << "[ " << rep << " ] ";
    for (auto&& value : gen) {
      cerr << value << " ";
    }
    cerr << endl;
  }
  cerr << endl;
}

// Enable `unique_ptr` to be an iterated type, where `nullptr` is the stopping
// criteria.
template<typename T>
struct generator_value_type<unique_ptr<T>> {
  using value_type = T;
};

class positive_int {
 public:
  using value_type = int;

  positive_int() {}
  positive_int(int value) : value_(value) {}
  // N.B. Seems to be *very* picky about using `move` and `T&&` here.
  int&& operator*() { return std::move(value_); }
  operator bool() const { return value_ >= 0; }
 private:
  int value_{-1};
};

class move_only_func {
 public:
  optional<int> operator()() {
    if (*value_ < 5) return (*value_)++;
    else return {};
  }
 private:
  unique_ptr<int> value_{new int{0}};
};

template <typename T>
class ref {
 public:
  using value_type = T&;

  ref() {}
  ref(T& value) : ptr_(&value) {}
  T& operator*() { return *ptr_; }
  operator bool() const { return ptr_ != nullptr; }
 private:
  T* ptr_{};
};

int main() {
  auto optional_gen = []() {
    return make_generator([i = 0]() mutable -> optional<int> {
      if (i < 10) return i++;
      return {};
    });
  };
  cerr << "simple:" << endl;
  print_container(optional_gen());

  cerr << "null:" << endl;
  print_container(null_generator<optional<int>>());

  cerr << "make_chain:" << endl;
  print_container(make_chain(optional_gen(), optional_gen()));

  cerr << "make_chain list: " << endl;
  print_container(make_chain_list<optional<int>>({
      optional_gen(), null_generator<optional<int>>(), optional_gen()}));

  cerr << "optional<unique_ptr>:" << endl;
  auto unique = make_generator(
    [i = 0]() mutable -> optional<unique_ptr<int>> {
      if (i < 5) return unique_ptr<int>(new int{i++});
      return {};
    });
  print_container(unique);

  cerr << "positive_int:" << endl;
  print_container(make_generator(
    [i = 0]() mutable -> positive_int {
      if (i < 3) return i++;
      return -1;
    }));

  cerr << " - implicit null sequence:" << endl;
  print_container(make_generator([]() mutable { return positive_int{}; }));

  cerr << "unique_ptr:" << endl;
  print_container(make_generator([i = 0]() mutable {
    if (i < 3) return unique_ptr<int>(new int{i++});
    return unique_ptr<int>();
  }));

  cerr << "move-only function:" << endl;
  print_container(make_generator(move_only_func{}));

  {
    cerr << "references:" << endl;
    int a = 0, b = 0;
    auto ref_gen = [&]() {
      return make_generator([&]() -> ref<int> {
        if (a == 0) return ++a;
        else if (b == 0) return b += 3;
        else return {};
      });
    };
    for (auto& cur : ref_gen()) {
      auto before = cur;
      cur *= 100;
      cerr << "before: " << before << ", cur: " << cur << endl;
    }
    cerr << "a: " << a << ", b: " << b << endl;
  }

  return 0;
}
