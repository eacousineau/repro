#include <string>

namespace sample {

/// Instantiations: double, string
template <typename T>
class SampleClass {
 public:
  explicit SampleClass(const T& value) : value_(value) {}
  const T& value() const { return value_; }
  void set_value(const T& value) { value_ = value; }

 private:
  T value_{};
};

}   // namespace sample
