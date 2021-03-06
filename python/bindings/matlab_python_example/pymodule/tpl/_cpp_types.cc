#include <pybind11/pybind11.h>

#include "python/bindings/pymodule/tpl/cpp_types.h"

PYBIND11_MODULE(_cpp_types, m) {
  py::class_<TypeRegistry> type_registry_cls(m, "_TypeRegistry");
  type_registry_cls
    .def(py::init<>())
    .def("GetPyTypeCanonical", &TypeRegistry::GetPyTypeCanonical)
    .def("GetName", [](const TypeRegistry *self, py::handle arg1) {
      return self->GetName(arg1);
    });
    // .def("GetName", py::overload_cast<py::handle>(&TypeRegistry::GetName))
  // Create instance.
  m.attr("_type_registry") = type_registry_cls();
}
