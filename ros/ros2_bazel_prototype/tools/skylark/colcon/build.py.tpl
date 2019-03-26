#!/usr/bin/env python3

from glob import glob
import os
from os import unlink, mkdir, symlink
from os.path import abspath, basename, dirname, isabs, isfile, join
from subprocess import run, PIPE
from shutil import rmtree
import sys

# Configured by Bazel.
CONFIG = %{config}
WORKSPACES = [abspath(x) for x in CONFIG["workspaces"]]

CC_PROPERTIES = [
    "INCLUDE_DIRECTORIES",
    "LINK_FLAGS",
    "LINK_DIRECTORIES",
    "LINK_LIBRARIES",
]


def template(src, dest, subs):
    with open(src) as f:
        text = f.read()
    for old, new in subs.items():
        text = text.replace(old, new)
    with open(dest, "w") as f:
        f.write(text)


def cc_get_transitives_libs(libs, libdirs, check=None):
    # Gets transitive deps of `libs` that aren't already in there.
    # (kinda dumb that Bazel / CMake doesn't figure this out, but meh...)
    # May be sketchy w.r.t. ordering...
    ldd_env = dict(LD_LIBRARY_PATH=":".join(libdirs))
    check_libs = [lib for lib in libs if check(lib)]
    lines = run(
        ["ldd"] + check_libs, env=ldd_env, check=True,
        stdout=PIPE, encoding="utf8").stdout.strip().split("\n")
    # Use libnames to permit shadowing (when useful).
    libnames = [basename(lib) for lib in libs]
    new_libs = []
    for line in lines:
        line = line.strip()
        if " => " not in line:
            continue
        _, _, lib, _ = line.split()
        libname = basename(lib)
        if check(lib) and libname not in libnames:
            print("Transitive: {}".format(lib))
            assert lib not in libs and lib not in new_libs
            new_libs.append(lib)
            libnames.append(libname)
    return new_libs


def cc_libdir_order_preference_sort(xs):
    # Go through and bubble up each thing.
    xs = list(xs)
    out = []
    for pref in WORKSPACES:
        prefix = abspath(pref) + "/"
        for x in list(xs):
            if x.startswith(prefix):
                out.append(x)
                xs.remove(x)
    # Add remaining.
    out += xs
    return out


def py_imports_sys():
    v = sys.version_info
    # dist-packages?
    pylib_relpath = "lib/python{}.{}/site-packages".format(v.major, v.minor)
    return [join(x, pylib_relpath) for x in WORKSPACES]


def cc_configure():
    template("CMakeLists.txt.in", "CMakeLists.txt", {
        "@NAME@": CONFIG["name"],
        "@PACKAGES@": " ".join(CONFIG["cc_packages"]),
        "@PROPERTIES@": " ".join(CC_PROPERTIES),
    })
    unlink("CMakeLists.txt.in")
    with open("empty.cc", "w") as f:
        f.write("")
    mkdir("build")
    cache_entries = dict(CONFIG["cc_cache_entries"])
    assert "CMAKE_PREFIX_PATH"  not in cache_entries
    cache_entries.update({"CMAKE_PREFIX_PATH": ";".join(WORKSPACES)})
    cmake_flags = ["-D{}={}".format(k, v) for k, v in cache_entries.items()]
    cmake_env = {
        "PATH": "/usr/local/bin:/usr/bin:/bin",
        "PYTHONPATH": ":".join(py_imports_sys()),
    }
    run(["cmake", ".."] + cmake_flags, check=True, cwd="build", env=cmake_env)
    unlink("empty.cc")
    props = dict()
    with open("build/props.txt") as f:
        for line in f:
            prop, value = line.strip().split("=")
            props[prop] = value and value.split(";") or []
    rmtree("build")
    mkdir("include")
    includes = []
    for include_sys in props["INCLUDE_DIRECTORIES"]:
        assert isabs(include_sys), include_sys
        include = join("include", include_sys.replace("/", "_"))
        symlink(include_sys, include)
        includes.append(include)
    linkopts = list(props["LINK_FLAGS"])
    libdirs = list(props["LINK_DIRECTORIES"])
    libs = list(props["LINK_LIBRARIES"])
    libdirs_ignore = {"/usr/lib", "/usr/lib/x86_64-linux-gnu"}
    for lib in libs:
        if isabs(lib):
            libdir = dirname(lib)
            if libdir not in libdirs and libdir not in libdirs_ignore:
                libdirs.append(libdir)
    libdirs = cc_libdir_order_preference_sort(libdirs)
    libs = cc_libdir_order_preference_sort(libs)
    print("\n".join(libs))
    for libdir in libdirs:
        linkopts += ["-L" + libdir, "-Wl,-rpath " + libdir]
    libs += cc_get_transitives_libs(
        libs, libdirs, check=lambda lib: dirname(lib) in libdirs)
    for lib in libs:
        linkopts += ["-l" + lib]
    # TODO(eric.cousineau): How do we strip out unused shared libraries like
    # CMake does???
    return {
        "%{cc_includes}": repr(includes),
        "%{cc_linkopts}": repr(linkopts),
        "%{cc_deps}": repr(CONFIG["cc_deps"]),
    }, libdirs


def py_generate_lib_dep_manifest(out_dir, libdirs):
    # This may be dumb. Meh.

    def check(lib):
        cur = dirname(lib) + "/"
        for libdir in libdirs:
            if cur.startswith(libdir + "/"):
                return True
        return False

    lib_dep_manifest = {}
    for pkg in CONFIG["py_packages"]:
        # Find the appropriate import.
        for import_sys in py_imports_sys():
            pkg_dir = join(import_sys, pkg)
            if isfile(join(pkg_dir, "__init__.py")):
                break
        else:
            print("Could not find py pkg dir for '{}'".format(pkg))
            sys.exit(1)
        pkg_libs = glob(join(pkg_dir, "**/*.so"), recursive=True)
        lib_deps = []
        for lib in pkg_libs:
            # May be hard to combine leaf these togethre... Meh for now.
            lib_deps += [lib] + cc_get_transitives_libs([lib], libdirs, check)
        lib_dep_manifest[pkg] = list(reversed(lib_deps))
    hack_file = join(
        out_dir, "_{}_bazel_lib_dep_manifest.py".format(CONFIG["name"]))
    with open(hack_file, "w") as f:
        f.write(r'''"""
Hacky auto-generated manifest loading.
"""
import ctypes

_lib_dep_manifest = %{lib_dep_manifest}

def preload(pkg_list):
    """
    Hack to awkwardly permit RPATH-like behavior... from python... yeah...
    """
    libs_loaded = set()
    for pkg in pkg_list:
        for lib_dep in _lib_dep_manifest[pkg]:
            if lib_dep in libs_loaded:
                continue
            print("load ", lib_dep)
            ctypes.cdll.LoadLibrary(lib_dep)
            libs_loaded.add(lib_dep)
'''.replace("%{lib_dep_manifest}", repr(lib_dep_manifest)))


def py_configure(libdirs):
    # Just symlink for now.
    os.mkdir("py")
    imports = []
    # Add existing system imports.
    for import_sys in py_imports_sys():
        import_ = join("py", import_sys.replace("/", "_"))
        symlink(import_sys, import_)
        imports.append(import_)
    # Generate hack manifest.
    gen_import = "py/gen"
    os.mkdir(gen_import)
    py_generate_lib_dep_manifest(gen_import, libdirs)
    imports.append(gen_import)
    # Done.
    return {
        "%{py_imports}": repr(imports),
        "%{py_deps}": repr(CONFIG["py_deps"]),
    }


def main():
    vars = {}
    cc_vars, libdirs = cc_configure()
    vars.update(cc_vars)
    vars.update(py_configure(libdirs))
    template("BUILD.bazel.tpl", "BUILD.bazel", vars)
    unlink("BUILD.bazel.tpl")


if __name__ == "__main__":
    main()
