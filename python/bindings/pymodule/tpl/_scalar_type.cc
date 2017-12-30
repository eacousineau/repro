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
#include "python/bindings/pymodule/tpl/cpp_tpl_types.h"

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
    PYBIND11_OVERLOAD_PURE(U, BaseT, pure, value);
  }
  U optional(T value) const override {
    PYBIND11_OVERLOAD(U, BaseT, optional, value);
  }
};


template <typename T, typename U>
void call_method(const Base<T, U>& base) {
  base.dispatch(T{10});
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

template <typename ... Args>
struct get_py_types {
  auto run() {
    return TypeRegistry::GetPyInstance().GetPyTypes<Args...>();
  }
};


template <
    template <typename...> class Tpl>
struct RegisterInstantiations {
  template <typename MetaPack, typename AddTpl>
  void run(
      py::module m, py::object tpl, const AddTpl& register,
      MetaPack packs = {}) {
    MetaPack::template visit<RegisterInstantiations>(m, tpl, register);
  }

 private:
  template <typename Pack, typename AddTpl>
  void visit(
      py::module m, py::object tpl, const AddTpl& register) {
    // Register instantiation in `pybind`, using lambda `auto`-friendly syntax.
    auto py_cls = register(Pack{});
    // Register template class `py_tpl`.
    auto type_tup = typename Pack::template bind<get_py_types>::run();
    tpl.attr("add_class")(type_tup, py_cls);
  }

  template <typename ... Ts>
  friend class type_pack<Ts...>;
};


template <
    template <typename...> class Tpl, typename Converter>
struct RegisterConversions {
  template <
      typename ToPack,
      typename FromMetaPack>
  static void run(
      auto& py_cls, py::object tpl,
      ToPack to_pack = {}, FromMetaPack from_packs = {}) {
    // Add conversion mechanisms.
    if (!py::hasattr(tpl, "_add_py_converter_map")) {
      tpl.attr("_add_py_converter_map") = py::dict();
    }
    FromMetaPack::template visit_if<
        RegisterConversions, is_different_from<ToPack>>(py_cls, tpl, to_pack);
  }

 private:
  template <typename ToPack>
  struct is_different_from {
    template <typename FromPack>
    using check = std::logical_not<std::is_same<ToPack, FromPack>::value>;
  };

  template <
      typename FromPack,  // iterator
      typename ToPack
      >
  static void visit(auto& py_cls, py::object tpl, ToPack to_pack) {
    // For each conversion available:
    // Register base conversion(s).
    using To = typename ToPack::template bind<Tpl>;
    auto to_tup = typename ToPack::template bind<get_py_types>::run();
    using ToPtr = std::unique_ptr<To>;
    using From = typename FromPack::template bind<Tpl>;
    auto from_tup = typename FromPack::template bind<get_py_types>::run();
    // Add Python converter function, but bind using Base C++ overloads via pybind.
    auto add_py_converter = [](Converter* converter, py::function py_func) {
      // Add type information.
      using ConversionFunc = std::function<ToPtr(const From&)>;
      auto cpp_func = py::cast<ConversionFunc>(py_func);
      // Add using overload.
      converter->Add(cpp_func);
    };
    // Register function dispatch.
    auto key = py::make_tuple(to_tup, from_tup);
    tpl.attr("_add_py_converter_map")[key] = py::cpp_function(add_py_converter);
    // Add Python conversion.
    py_cls
      .def(py::init<const From&>());
    // End: Scalar conversion.
  }

  template <typename ... Ts>
  friend class type_pack<Ts...>;
};


PYBIND11_MODULE(_scalar_type, m) {
  m.doc() = "Simple check on scalar / template types";

  py::handle py_tpl = py::module::import("pymodule.tpl.py_tpl");

  // Register BaseConverter...
  py::class_<BaseConverter> base_converter(m, "BaseConverter");
  base_converter
    .def(py::init<>())
    .def(
      "Add",
      [tpl](BaseConverter* self,
             py::tuple params_to, py::tuple params_from,
             py::function py_converter) {
        // Assume we have canonical Python types.
        // Find our conversion function using these types.
        auto key = py::make_tuple(params_to, params_from);
        auto add_py_converter = tpl.attr("_add_py_converter_map")[key];
        // Now register the converter.
        add_py_converter(self, py_converter);
      });

  py::handle tpl_cls = py_tpl.attr("Template");

  py::object tpl = tpl_cls("Base");
  m.attr("BaseTpl") = tpl;

  // Add instantiations and conversion mechanisms.
  using AllParam = type_pack<
      type_pack<int, double>,
      type_pack<double, int>
      >;

  auto base_py = [m, tpl](auto param_pack) {
    // Extract parameters.
    using Pack = decltype(param_pack);
    using T = typename Pack::template get_type<0>;
    using U = typename Pack::template get_type<1>;
    // Typedef classes.
    typedef Base<T, U> BaseT;
    typedef PyBase<T, U> PyBaseT;
    // Define class.
    string name = nice_type_name<BaseT>();
    // N.B. This  name will be overwritten by `tpl.add_class(...)`.
    py::class_<BaseT, PyBaseT> py_cls(m, name.c_str());
    py_cls
      .def(py::init<T, U, std::unique_ptr<BaseConverter>>(),
           py::arg("t"), py::arg("u"), py::arg("converter") = nullptr)
      .def("t", &BaseT::t)
      .def("u", &BaseT::u)
      .def("pure", &BaseT::pure)
      .def("optional", &BaseT::optional)
      .defbase_py("dispatch", &BaseT::dispatch);

    // Can't get `overload_cast` to infer `Return` type.
    // Have to explicitly cast... :(
    m.def("call_method", static_cast<void(*)(const BaseT&)>(&call_method));

    // Register conversions.
    RegisterConversions<Base, BaseConverter>::run(
        py_cls, tpl, param_pack, AllParam{});
    return py_cls;
  };

  RegisterInstantiations<Base>::run(
      m, base_py, tpl, AllParam{});

  // Default instantiation.
  m.attr("Base") = tpl.attr("get_class")();

  m.def("do_convert", &do_convert);
  m.def("take_ownership", &take_ownership);
}

}  // namespace scalar_type
