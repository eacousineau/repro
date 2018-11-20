# -*- python -*-

def pybind_py_library(
        name,
        cc_srcs = [],
        cc_deps = [],
        cc_so_name = None,
        cc_binary_rule = native.cc_binary,
        py_srcs = [],
        py_deps = [],
        py_imports = [],
        py_library_rule = native.py_library,
        visibility = None,
        testonly = None):
    """Declares a pybind11 Python library with C++ and Python portions.

    @param cc_srcs
        C++ source files.
    @param cc_deps (optional)
        C++ dependencies.
        At present, these should be libraries that will not cause ODR
        conflicts (generally, header-only).
    @param cc_so_name (optional)
        Shared object name. By default, this is `${name}`, so that the C++
        code can be then imported in a more controlled fashion in Python.
        If overridden, this could be the public interface exposed to the user.
    @param py_srcs (optional)
        Python sources.
    @param py_deps (optional)
        Python dependencies.
    @param py_imports (optional)
        Additional Python import directories.
    @return struct(cc_so_target = ..., py_target = ...)
    """
    py_name = name
    if not cc_so_name:
        cc_so_name = name

    # TODO(eric.cousineau): See if we can keep non-`*.so` target name, but
    # output a *.so, so that the target name is similar to what is provided.
    cc_so_target = cc_so_name + ".so"

    # Add C++ shared library.
    cc_binary_rule(
        name = cc_so_target,
        srcs = cc_srcs,
        # This is how you tell Bazel to create a shared library.
        linkshared = 1,
        linkstatic = 1,
        copts = [
            # GCC and Clang don't always agree / succeed when inferring storage
            # duration (#9600). Workaround it for now.
            "-Wno-unused-lambda-capture",
        ],
        # Always link to pybind11.
        deps = [
            "@pybind11",
        ] + cc_deps,
        testonly = testonly,
        visibility = visibility,
    )

    # Add Python library.
    py_library_rule(
        name = py_name,
        data = [cc_so_target],
        srcs = py_srcs,
        deps = py_deps,
        imports = py_imports,
        testonly = testonly,
        visibility = visibility,
    )
    return struct(
        cc_so_target = cc_so_target,
        py_target = py_name,
    )

def _generate_pybind_documentation_header_impl(ctx):
    compile_flags = []
    transitive_headers_depsets = []
    package_headers_depsets = []
    for target in ctx.attr.targets:
        if hasattr(target, "cc"):
            # Note that target.cc.compile_flags does not include copts added
            # to the target or on the command line (including via rc file).
            compile_flags += [
                compile_flag.replace(" ", "")
                for compile_flag in target.cc.compile_flags
            ]
            transitive_headers_depsets.append(target.cc.transitive_headers)

            # Find all headers provided by the drake_cc_package_library,
            # i.e., the set of transitively-available headers that exist in
            # the same Bazel package as the target.
            package_headers_depsets.append(depset(direct = [
                transitive_header
                for transitive_header in target.cc.transitive_headers
                if (target.label.package == transitive_header.owner.package and
                    target.label.workspace_root == transitive_header.owner.workspace_root)  # noqa
            ]))

    transitive_headers = depset(transitive = transitive_headers_depsets)
    package_headers = depset(transitive = package_headers_depsets)

    mkdoc = ctx.file._mkdoc

    args = ctx.actions.args()
    args.add_all(compile_flags, uniquify = True)
    args.add("-output=" + ctx.outputs.out.path)
    args.add("-quiet")
    args.add("-root-name=" + ctx.attr.root_name)
    for p in ctx.attr.exclude_hdr_patterns:
        args.add("-exclude-hdr-patterns=" + p)

    # Replace with ctx.fragments.cpp.cxxopts in Bazel 0.17+.
    args.add("-std=c++14")
    args.add("-w")
    args.add_all(package_headers)

    ctx.actions.run_shell(
        outputs = [ctx.outputs.out],
        inputs = transitive_headers,
        tools = [mkdoc],
        arguments = [args],
        command = "{} $@".format(mkdoc.path),
    )

# Generates a header that defines variables containing a representation of the
# contents of Doxygen comments for each class, function, etc. in the
# transitive headers of the given targets.
# @param targets Targets with header files that should have documentation
# strings generated.
# @param root_name Name of the root struct in generated file.
# @param exclude_hdr_patterns Headers whose symbols should be ignored. Can be
# glob patterns.
generate_pybind_documentation_header = rule(
    attrs = {
        "targets": attr.label_list(
            mandatory = True,
        ),
        "_mkdoc": attr.label(
            default = Label("//pybind11_ext:mkdoc"),
            allow_single_file = True,
            cfg = "host",
            executable = True,
        ),
        "out": attr.output(mandatory = True),
        "root_name": attr.string(default = "pydrake_doc"),
        "exclude_hdr_patterns": attr.string_list(),
    },
    fragments = ["cpp"],
    implementation = _generate_pybind_documentation_header_impl,
    output_to_genfiles = True,
)
