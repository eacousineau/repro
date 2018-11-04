#include <cassert>
#include <functional>
#include <experimental/optional>

using std::experimental::optional;

/**
Provides a generator class that can be iterated upon:
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
Imitates Python generator expressions:
- begin() returns an iterator that has evaluated the first expression
- end() refers to the end of iteration (no more values to consume)
- Can only be iterated once; afterwards, it will always return
  `begin() == end()`.
 */
template <
    typename T, typename result_t = optional<T>,
    typename Func = std::function<result_t()>>
class generator_t {
 public:
  generator_t(Func&& func)
      : func_(std::forward<Func>(func)) {}

  template <typename OtherFunc>
  generator_t(generator_t<T, result_t, OtherFunc>&& other)
      : func_(std::move(other.func_)),
        value_(std::move(other.value_)) {}

  class iterator {
   public:
    iterator() {}
    iterator(generator_t* parent, bool is_end) : parent_(parent) {
      assert(valid());
      if (!is_end) {
        // Will increment `count` to 1 if it's a valid value, and thus will not
        // equal `end()`.
        ++(*this);
      }
    }
    bool valid() const {
      return parent_ != nullptr;
    }
    T&& operator*() {
      assert(valid());
      return std::forward<T>(*parent_->value_);
    }
    void operator++() {
      assert(valid());
      ++count_;
      if (!parent_->store_next()) {
        count_ = 0;
      }
    }
    bool operator==(const iterator& other) {
      return parent_ == other.parent_ && count_ == other.count_;
    }
    bool operator!=(const iterator& other) { return !(*this == other); }
   private:
    generator_t* parent_{};
    int count_{0};
  };

  iterator begin() { return iterator(this, false); }
  iterator end() { return iterator(this, true); }

  // May invalidate iterator state; can restore iterator state with `++iter`.
  result_t next() {
    store_next();
    return std::move(value_);
  }
 private:
  friend class iterator;
  template <typename A, typename B, typename C>
  friend class generator_t;
  bool store_next() {
    value_ = func_();
    return bool{value_};
  }
  Func func_;
  result_t value_;
};

template <
    typename T, typename result_t = optional<T>,
    typename Func>
auto generator(Func&& func) {
  return generator_t<T, result_t, Func>(std::forward<Func>(func));
}

template <typename T, typename result_t = optional<T>>
auto null_generator() {
  return generator<T, result_t>([]() { return result_t{}; });
}

int visit_count = 0;
template <typename T>
void visit(T&&) { ++visit_count; }

template <typename Container>
void visit_container(Container&& gen) {
  for (int rep : {0, 1}) {
    for (auto&& value : gen) {
      visit(value);
    }
  }
}

int main() {
  auto simple_gen = []() {
    return generator<int>([i = 0]() mutable -> optional<int> {
      if (i < 10) return i++;
      return {};
    });
  };
  visit_container(simple_gen());
  visit_container(null_generator<int>());
  return 0;
}
