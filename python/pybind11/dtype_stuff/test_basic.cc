#include <iostream>

using std::cout;
using std::endl;

#include <pybind11/pybind11.h>
#include <pybind11/embed.h>

#define NPY_NO_DEPRECATED_API NPY_1_7_API_VERSION
#include <numpy/arrayobject.h>
#include <numpy/ufuncobject.h>

namespace py = pybind11;

class Custom {
 public:
  using Self = Custom;

  Custom(double value) : value_{value} {}
  Custom(const Custom&) = default;
  Custom& operator=(const Custom&) = default;

  Self& operator+=(const Self& rhs) { value_ += rhs.value_; return *this; } 
  Self& operator+=(double rhs) { value_ += rhs; return *this; }
  Self operator+(Self rhs) { return rhs += *this; }
  Self operator+(double rhs) { return Self{*this} += rhs; }

  double value() const { return value_; }

 private:
  double value_{};
};

void module(py::module m) {}

int main() {
  py::scoped_interpreter guard;

  Custom a{1}, b{2};
  a += b + 3;
  cout << a.value() << endl;

  return 0;
}
