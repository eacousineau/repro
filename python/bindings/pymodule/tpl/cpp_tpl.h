#pragma once

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/functional.h>

#include "cpp/name_trait.h"
#include "cpp/type_pack.h"
#include "python/bindings/pymodule/tpl/cpp_tpl_types.h"

// TODO: Figure out how to handle literals...

namespace py = pybind11;

template <typename ... Ts>
inline py::tuple get_py_types(type_pack<Ts...> = {}) {
  return TypeRegistry::GetPyInstance().GetPyTypes<Ts...>();
}

py::object InitOrGetTemplate(
    py::handle scope, const std::string& name,
    const std::string& template_type, py::tuple create_extra = py::tuple()) {
  // Add property / descriptor if it does not already exist.
  // This works for adding to classes since `TemplateMethod` acts as a
  // descriptor.
  // @note Overloads won't be allowed with templates. If it is needed,
  // see `py::sibling(...)`.
  py::object tpl = py::getattr(scope, name.c_str(), py::none());
  if (tpl.is(py::none())) {
    py::handle cpp_tpl = py::module::import("pymodule.tpl.cpp_tpl");
    tpl = cpp_tpl.attr(template_type.c_str())(name, *create_extra);
    py::setattr(scope, name.c_str(), tpl);
  }
  return tpl;
}


template <typename ... Ts>
void AddInstantiation(
    py::object tpl, py::object obj,
    type_pack<Ts...> param = {}) {
  tpl.attr("add_instantiation")(get_py_types(param), obj);
}

template <typename PyClass, typename ... Ts>
py::object AddTemplateClass(
    py::handle scope, const std::string& name,
    PyClass& py_class,
    const std::string& default_inst_name = "",
    type_pack<Ts...> param = {},
    py::handle parent = py::none()) {
  py::object tpl = InitOrGetTemplate(
      scope, name, "TemplateClass", py::make_tuple(parent));
  AddInstantiation(tpl, py_class, param);
  if (!default_inst_name.empty() &&
      !py::hasattr(scope, default_inst_name.c_str())) {
    scope.attr(default_inst_name.c_str()) = py_class;
  }
  return tpl;
}

template <typename Func, typename ... Extra, typename ... Ts>
py::object AddTemplateFunctionImpl(
    py::object tpl, Func&& func, type_pack<Ts...> param, Extra... extra) {
  // Ensure that pybind is aware that it's a function.
  std::string instantiation_name =
      py::cast<std::string>(
          tpl.attr("_get_instantiation_name")(get_py_types(param)));
  py::object py_func = py::cpp_function(
        std::forward<Func>(func),
        py::name(instantiation_name.c_str()), extra...);
  AddInstantiation(tpl, py_func, param);
  return tpl;
}

// WARNING: If you forget `param`, then it'll assume an empty set, and create a
// runtime error :(

template <typename ... Ts, typename Func>
py::object AddTemplateFunction(
    py::handle scope, const std::string& name, Func&& func,
    type_pack<Ts...> param = {}) {
  py::object tpl = InitOrGetTemplate(scope, name, "TemplateFunction");
  return AddTemplateFunctionImpl(
      tpl, std::forward<Func>(func), param);
}

template <typename ... Ts, typename Func>
py::object AddTemplateMethod(
    py::handle scope, const std::string& name, Func&& func,
    type_pack<Ts...> param = {}) {
  py::object tpl = InitOrGetTemplate(
      scope, name, "TemplateMethod", py::make_tuple(scope));
  return AddTemplateFunctionImpl(
      tpl, std::forward<Func>(func), param, py::is_method(scope));
}


template <
    typename Check = always_true, typename ParamList = void,
    typename InstantiationFunc = void>
void IterTemplate(
    const InstantiationFunc& instantiation_func, ParamList = {}) {
  ParamList::template visit_if<Check, no_tag>([&](auto param) {
    instantiation_func(param);
  });
}
