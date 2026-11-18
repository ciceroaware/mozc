# Copyright 2010-2021, Google Inc.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#     * Redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above
# copyright notice, this list of conditions and the following disclaimer
# in the documentation and/or other materials provided with the
# distribution.
#     * Neither the name of Google Inc. nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

"""EXPERIMENTAL: Repository rule to borrow ATL from the local Visual Studio.

hermetic-llvm (through the windows_support module) downloads the MSVC runtime
and the Windows SDK, but not ATL, which is a separate Visual Studio component.
This rule exposes the ATL headers and libraries of the locally installed Visual
Studio (located with BAZEL_VC) as @atl//:atl so that x64/arm64 targets built
with hermetic-llvm can keep using ATL. This is NOT hermetic.

x86 (32-bit) targets are still built with the locally installed toolchain,
which already has ATL in its INCLUDE/LIB, so @atl//:atl is empty for them.
"""

_STUB_BUILD = """
load("@rules_cc//cc:cc_library.bzl", "cc_library")

cc_library(
    name = "atl",
    visibility = ["//visibility:public"],
)
"""

_BUILD_TEMPLATE = """
load("@rules_cc//cc:cc_library.bzl", "cc_library")

cc_library(
    name = "atl_impl",
    hdrs = glob(["include/**"]),
    includes = ["include"],
    # ATL headers request "atls.lib" with '#pragma comment(lib, ...)'.
    additional_linker_inputs = select({{
        "@platforms//cpu:arm64": glob(["lib/arm64/*.lib"]),
        "@platforms//cpu:x86_64": glob(["lib/x64/*.lib"]),
    }}),
    linkopts = select({{
        "@platforms//cpu:arm64": ["/LIBPATH:external/{repo_name}/lib/arm64"],
        "@platforms//cpu:x86_64": ["/LIBPATH:external/{repo_name}/lib/x64"],
    }}),
    target_compatible_with = ["@platforms//os:windows"],
)

cc_library(
    name = "atl",
    visibility = ["//visibility:public"],
    deps = select({{
        "@platforms//cpu:x86_32": [],
        "//conditions:default": [":atl_impl"],
    }}),
)
"""

def _version_key(name):
    return [int(x) if x.isdigit() else 0 for x in name.split(".")]

def _find_atl_dir(repo_ctx):
    bazel_vc = repo_ctx.getenv("BAZEL_VC")
    if not bazel_vc:
        return None
    tools_dir = repo_ctx.path(bazel_vc.strip("\"")).get_child("Tools", "MSVC")
    if not tools_dir.exists:
        return None

    # Respect BAZEL_VC_FULL_VERSION in the same way as rules_cc does.
    full_version = repo_ctx.getenv("BAZEL_VC_FULL_VERSION")
    candidates = [
        d
        for d in tools_dir.readdir()
        if d.get_child("atlmfc", "include", "atlbase.h").exists and
           (not full_version or d.basename == full_version)
    ]
    if not candidates:
        return None
    latest = sorted(candidates, key = lambda d: _version_key(d.basename))[-1]
    return latest.get_child("atlmfc")

def _atl_repository_impl(repo_ctx):
    is_windows = repo_ctx.os.name.lower().startswith("win")
    atl_dir = _find_atl_dir(repo_ctx) if is_windows else None
    if atl_dir == None:
        repo_ctx.file("BUILD.bazel", _STUB_BUILD)
        return

    repo_ctx.symlink(atl_dir.get_child("include"), "include")
    repo_ctx.symlink(atl_dir.get_child("lib"), "lib")
    repo_ctx.file("BUILD.bazel", _BUILD_TEMPLATE.format(
        repo_name = repo_ctx.name,
    ))

atl_repository = repository_rule(
    implementation = _atl_repository_impl,
    configure = True,
    local = True,
    environ = [
        "BAZEL_VC",
        "BAZEL_VC_FULL_VERSION",
    ],
)
