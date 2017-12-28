// Purpose: Test what binding different scalar types (template arguments) might
// look like with `pybind11`.
// Specifically, having a base class of <T, U>, and seeing if pybind11 can bind
// it "easily".

#include <cstddef>
#include <cmath>
#include <sstream>
#include <string>

#include <pybind11/cast.h>
#include <pybind11/eval.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/functional.h>

#include "cpp/name_trait.h"
#include "cpp/simple_converter.h"

namespace py = pybind11;
using namespace py::literals;
using namespace std;

using namespace simple_converter;

namespace scalar_type {

template <typename T = float, typename U = int16_t>
class Base;

}

using scalar_type::Base;
NAME_TRAIT_TPL(Base)

namespace scalar_type {

typedef SimpleConverter<Base> BaseConverter;

// Simple base class.
template <typename T, typename U>
class Base {
 public:
  Base(T t, U u, std::unique_ptr<BaseConverter> converter = nullptr)
    : t_(t),
      u_(u),
      converter_(std::move(converter)) {
    if (!converter_) {
      converter_.reset(new BaseConverter());
      typedef Base<double, int> A;
      typedef Base<int, double> B;
      converter_->AddCopyConverter<A, B>();
      converter_->AddCopyConverter<B, A>();
    }
  }

  template <typename Tc, typename Uc>
  Base(const Base<Tc, Uc>& other)
    : Base(static_cast<T>(other.t_),
           static_cast<U>(other.u_),
           std::make_unique<BaseConverter>(*other.converter_)) {}

  virtual ~Base() {
    cout << "Base::~Base" << endl;
  }

  T t() const { return t_; }
  U u() const { return u_; }

  virtual U pure(T value) const { return U{}; } // = 0 -- Do not use for concrete converter example.
  virtual U optional(T value) const {
    cout << py_name() << endl;
    return static_cast<U>(value);
  }

  U dispatch(T value) const {
    cout << "cpp.dispatch [" << py_name() << "]:\n";
    cout << "value = " << value << endl;
    cout << " .t = " << t() << endl;
    cout << " .u = " << u() << endl;
    cout << "  ";
    U pv = pure(value);
    cout << "  = " << pv << endl;
    cout << "  ";
    U ov = optional(value);
    cout << "  = " << ov << endl;
    return pv + ov;
  }

  // TODO: Use `typeid()` and dynamic dispatching?
  static string py_name() {
    return "Base[" + name_trait<T>::name() +
      ", " + name_trait<U>::name() + "]";
  }

  template <typename To>
  std::unique_ptr<To> DoTo() const {
    return converter_->Convert<To>(*this);
  }

 private:
  template <typename Tc, typename Uc> friend class Base;

  T t_{};
  U u_{};
  std::unique_ptr<BaseConverter> converter_;
};


template <typename T, typename U>
class PyBase : public py::wrapper<Base<T, U>> {
 public:
  typedef Base<T, U> BaseT;
  typedef py::wrapper<Base<T, U>> BaseW;

  using BaseW::BaseW;

  U pure(T value) const override {
    // Do NOT use `BWrap` here as pybind uses direct RTTI on the supplied type.
    PYBIND11_OVERLOAD(U, BaseT, pure, value);
  }
  U optional(T value) const override {
    PYBIND11_OVERLOAD(U, BaseT, optional, value);
  }
};


template <typename T, typename U>
void call_method(const Base<T, U>& base) {
  base.dispatch(T{});
}

std::unique_ptr<Base<double, int>> do_convert(const Base<int, double>& value) {
  cout << "Attempt conversion" << endl;
  std::unique_ptr<Base<double, int>> out(value.DoTo<Base<double, int>>());
  // auto out = std::make_unique<Base<double, int>>(8.5, 10);  // Not equivalent...
  // Try to create an instance of `ChildTpl`.
  cout << "Got it" << endl;
  return out;
}


// How can this work?
std::unique_ptr<Base<double, int>> take_ownership(py::function factory) {
  cout << "cpp call" << endl;
  py::object out_py = factory();
  cout << "cpp convert" << endl;
  return py::cast<std::unique_ptr<Base<double, int>>>(std::move(out_py));
}

/// Retuns the PyTypeObject from the resultant expression type.
/// @note This may incur a copy due to implementation details.
template <typename T>
py::handle py_type_eval(const T& value) {
  auto locals = py::dict("model_value"_a=value);
  return py::eval("type(model_value)", py::globals(), locals);
}

template <typename T, bool is_default_constructible = false>
struct py_type_impl {
  static py::handle run() {
    // Check registered C++ types.

    // NOTE: This seems to be implemented by `get_type_info(type_info)
    const auto& internals = py::detail::get_internals();
    const auto& types_cpp = internals.registered_types_cpp;
    auto iter = types_cpp.find(std::type_index(typeid(T)));
    if (iter != types_cpp.end()) {
      auto info = static_cast<const py::detail::type_info*>(iter->second);
      return py::handle((PyObject*)info->type);
    } else {
      return py::handle();
    }
  }
};

template <typename T>
struct py_type_impl<T, true> {
  static py::handle run() {
    // First check registration.
    py::handle attempt = py_type_impl<T, false>::run();
    if (attempt) {
      return attempt;
    } else {
      // Next, check through default construction and using cast
      // implementations.
      try {
        return py_type_eval(T{});
      } catch (const py::cast_error&) {
        return py::handle();
      }
    }
  }
};

// http://pybind11.readthedocs.io/en/stable/advanced/pycpp/object.html#casting-back-and-forth
// http://pybind11.readthedocs.io/en/stable/advanced/pycpp/utilities.html
// http://pybind11.readthedocs.io/en/stable/advanced/misc.html

// registered_types_py
// registered_types_cpp
// registered_instances

/// Gets the PyTypeObject representing the Python-compatible type for `T`.
/// @note If this is a custom type, ensure that it has been fully registered
/// before calling this.
/// @note If this is a builtin type, note that some information may be lost.
///   e.g. T=vector<int>  ->  <type 'list'>
///        T=int64_t      ->  <type 'long'>
///        T=int16_t      ->  <type 'long'>
///        T=double       ->  <type 'float'>
///        T=float        ->  <type 'float'>
template <typename T>
py::handle py_type() {
  py::handle type =
        py_type_impl<T, std::is_default_constructible<T>::value>::run();
  if (type) {
    return type;
  } else {
    throw std::runtime_error("Could not find type of: " + py::type_id<T>());
  }
}

struct A {
  explicit A(int x) {}
};

struct reg_info {
  // py::tuple params  ->  size_t (type_pack<Tpl<...>>::hash)
  py::dict mapping;

  // For conversions.
  typedef std::function<void(BaseConverter*, py::function)> PyFunc;
  std::map<BaseConverter::Key, PyFunc> conv_mapping;
};

template <typename T, typename U>
void register_base(py::module m, reg_info* info, py::object tpl) {
  string name = Base<T, U>::py_name();
  typedef Base<T, U> C;
  typedef PyBase<T, U> PyC;
  py::class_<C, PyC> base(m, name.c_str());
  base
    .def(py::init<T, U, std::unique_ptr<BaseConverter>>(),
         py::arg("t"), py::arg("u"), py::arg("converter") = nullptr)
    .def("t", &C::t)
    .def("u", &C::u)
    .def("pure", &C::pure)
    .def("optional", &C::optional)
    .def("dispatch", &C::dispatch);

  // Register template class.
  auto type_tup = py::make_tuple(py_type<T>(), py_type<U>());
  tpl.attr("add_class")(type_tup, base);

  // // Can't figure this out...
  // Can't get `overload_cast` to infer `Return` type.
  // Have to explicitly cast... :(
  typedef void (*call_method_t)(const Base<T, U>&);
  m.def("call_method", static_cast<call_method_t>(&call_method));

  // Begin: Scalar conversion
  size_t key = BaseConverter::hash<C>();
  info->mapping[type_tup] = key;

  // For each conversion available:
  // Register base conversion(s).
  using To = C;
  using ToPtr = std::unique_ptr<To>;
  using From = Base<U, T>;

  // Convert Py function to specific types, then erase.
  auto func_converter = [](BaseConverter* converter, py::function py_func) {
    using Func = std::function<ToPtr(const From&)>;
    // Add type information.
    auto cpp_func = py::cast<Func>(py_func);
    converter->Add(cpp_func);
  };
  auto conv_key = BaseConverter::get_key<To, From>();
  info->conv_mapping[conv_key] = func_converter;
  base
    .def(py::init<const From&>());
  // End: Scalar conversion.
}

PYBIND11_MODULE(_scalar_type, m) {
  m.doc() = "Simple check on scalar / template types";

  py::class_<A> a(m, "A");

  py::handle py_tpl = py::module::import("pymodule.tpl.py_tpl");
  py::handle tpl_cls = py_tpl.attr("Template");

  using arg = py::arg;

  py::object tpl = tpl_cls("Base", py::eval("int, float"));
  // No difference between (float, double) and (int16_t, int64_t)
  // Gonna use other combos.
  reg_info info;
  register_base<double, int>(m, &info, tpl);
  register_base<int, double>(m, &info, tpl);
  // Add to module.
  m.attr("BaseTpl") = tpl;
  // Default instantiation.
  m.attr("Base") = tpl.attr("get_class")();

  m.def("do_convert", &do_convert);

  // Register BaseConverter...
  py::class_<BaseConverter> base_converter(m, "BaseConverter");
  base_converter
    .def(py::init<>())
    .def(
      "Add",
      [info](BaseConverter* self,
             py::tuple params_to, py::tuple params_from,
             py::function py_converter) {
        // Get BaseConverter::Key from the paramerters.
        BaseConverter::Key key {
            py::cast<size_t>(info.mapping[params_to]),
            py::cast<size_t>(info.mapping[params_from])
          };
        // Allow pybind to convert the lambdas.
        auto func_converter = info.conv_mapping.at(key);
        // Now register the converter.
        func_converter(self, py_converter);
      });

  m.def("take_ownership", &take_ownership);
}

}  // namespace scalar_type
