#!/bin/bash

# Expose Bazel's Python paths, using path info gleaned from `env_info.output.txt`.
#
# Example:
# 
#    $ source setup_bazel_python_env.sh pydrake_type_binding_test

export-append () { 
    eval "export $1=\"\$$1:$2\""
}

target=$1
package="bindings"
target_src_dir="bindings/python"

workspace=$(bazel info workspace)
repo=$(basename $workspace)
bindir=${workspace}/bazel-bin

runfiles=${bindir}/${package}/${target}.runfiles

export-append PYTHONPATH ${runfiles}:${runfiles}/${repo}/${target_src_dir}:${runfiles}/${repo}
