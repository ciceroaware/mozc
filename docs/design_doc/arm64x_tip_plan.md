# Building the TIP DLL as an ARM64X binary

## Objective

Build the 64-bit TIP DLL for ARM64 Windows (`mozc_tip64x.dll`) as a single ARM64X binary with Bazel, so that one DLL can be loaded into both native ARM64 processes and emulated x64 processes without relying on DLL forwarding.

## Background

On ARM64 Windows, the 64-bit TIP needs to be loadable into both ARM64 and x64 processes. Previously this was done with three DLLs.

 * `mozc_tip64arm.dll`: ARM64 implementation.
 * `mozc_tip64.dll`: x64 implementation.
 * `mozc_tip64x.dll`: an [Arm64X pure forwarder DLL](https://learn.microsoft.com/en-us/windows/arm/arm64x-build#building-an-arm64x-pure-forwarder-dll), registered as `InprocServer32`. It has no code and forwards `DllGetClassObject` and `DllCanUnloadNow` to one of the above by bare module name.

A forward target is resolved only when the forwarded export is looked up (e.g. by `GetProcAddress`), through the normal DLL search order. That search includes the forwarder's own directory only if the forwarder was loaded with `LOAD_WITH_ALTERED_SEARCH_PATH` or `LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR`.

| `LoadLibraryExW` flags to load the forwarder | Forward target resolved? |
|---|---|
| `0` | no (`ERROR_PROC_NOT_FOUND`) |
| `LOAD_LIBRARY_SEARCH_SYSTEM32` | no |
| `LOAD_LIBRARY_SEARCH_DEFAULT_DIRS` | no |
| `LOAD_WITH_ALTERED_SEARCH_PATH` | yes |

COM usually loads in-proc servers with `LOAD_WITH_ALTERED_SEARCH_PATH | LOAD_LIBRARY_SAFE_CURRENT_DIRS`, but in some packaged processes it uses `LoadLibraryExW(path, nullptr, 0)`. This was observed with a debugger in `SystemSettings.exe`, where TSF activates the TIP through `msctf!CThreadInputMgr::_CreateTip` and `combase!CoCreateInstance`. As a result, the IME could not be activated in the Settings app and in the Start menu search box (`SearchHost.exe`). See https://github.com/google/mozc/discussions/1623 for the investigation.

## Design overview

`mozc_tip64x.dll` becomes a full ARM64X DLL: a single file that contains both ARM64 code and ARM64EC code (ARM64 instructions that follow the x64 ABI), with ordinary, non-forwarded exports. `mozc_tip64arm.dll` is no longer built, and `mozc_tip64.dll` is installed only on x64 Windows.

The DLL is built with LLVM. There are two ways to get an ARM64X binary out of LLVM, and both work with the static CRT, the STL, `DllMain` and static libraries.

1. Compile everything twice, once for `aarch64-pc-windows-msvc` and once for `arm64ec-pc-windows-msvc`, and link both sets of objects with `lld-link /machine:arm64x`.
2. Compile each source once with `clang-cl -marm64x`, which produces one hybrid object (a native ARM64 object with the ARM64EC object embedded in a `.obj.arm64ec` section), and link with `lld-link /machine:arm64x`. `llvm-lib` can archive such objects, and `lld-link` picks up both halves from the archive.

Option 2 is used because it fits Bazel much better: one toolchain in one configuration, and a plain `cc_binary` produces the ARM64X DLL. Option 1 would need a custom rule that links the outputs of two configurations.

## Detailed design

### LLVM tools

`clang-cl -marm64x` runs `llvm-objcopy` to embed the ARM64EC object into the ARM64 object. `build_tools/update_deps.py` extracts `llvm-objcopy.exe` together with `clang-cl.exe`, `llvm-lib.exe` and `lld-link.exe`.

```python
# llvm-objcopy.exe is used by clang-cl to merge the ARM64 and ARM64EC objects
# into a single ARM64X object when -marm64x is specified.
LLVM_BIN_FILES = ['clang-cl.exe', 'llvm-lib.exe', 'lld-link.exe', 'llvm-objcopy.exe']
```

### ARM64X clang-cl toolchain

The ARM64X toolchain is added to rules_cc's Windows toolchain template through the existing local patches (`src/bazel/rules_cc_*.patch`). It is a copy of the arm64 clang-cl toolchain with the same SDK and CRT paths, and the following differences.

```python
cc_toolchain_config(
    name = "clang_cl_arm64x",
    cpu = "arm64x_windows",
    compiler = "clang-cl",
    target_system_name = "aarch64-pc-windows-msvc",
    ...
    archiver_flags = ["/MACHINE:ARM64X"],
    default_compile_flags = ["--target=aarch64-pc-windows-msvc", "-marm64x"],
    default_link_flags = ["/MACHINE:ARM64X"],
    ...
)

toolchain(
    name = "cc-toolchain-arm64x_windows-clang-cl",
    ...
    target_compatible_with = [
        "@platforms//cpu:arm64",
        "@platforms//os:windows",
        "@@//:arm64x",
    ],
    toolchain = ":cc-compiler-arm64x_windows-clang-cl",
    toolchain_type = "@bazel_tools//tools/cpp:toolchain_type",
)
```

`arm64x_windows` is also added to `_use_msvc_toolchain()` in `windows_cc_toolchain_config.bzl`, and the following two changes are made there.

#### `-o<path>` instead of `/Fo<path>`

```python
flag_group(
    flags = ["-o%{output_file}"] if ctx.attr.cpu == "arm64x_windows" else ["/Fo%{output_file}"],
    expand_if_not_available = "output_preprocess_file",
),
```

This works around two issues in clang-cl 23.1.6.

 * With `-marm64x /Fo<path>`, clang-cl compiles both halves into the same `<path>` and then runs `llvm-objcopy` on that single file. The ARM64 half is lost, and the native view of the final DLL has no code and no exports. With `-o <path>` (or without an output option), clang-cl uses separate temporary files and merges them correctly.
 * In a response file, clang-cl rejects `-o` followed by the path as a separate argument (`argument to '-o' is missing`), while `-o<path>` works. Bazel always passes compiler arguments through a response file.

#### DEF file for both views

```python
flag_group(
    flags = ["/DEF:%{def_file_path}", "/ignore:4070"] + (
        ["/defArm64Native:%{def_file_path}"] if ctx.attr.cpu == "arm64x_windows" else []
    ),
    expand_if_available = "def_file_path",
),
```

With `/machine:arm64x`, `/DEF` applies only to the ARM64EC (x64) view, and the native view needs `/defArm64Native`. Doing this in the toolchain also covers the DLLs built by `mozc_cc_win32_library`, which exist only to produce import libraries (e.g. for `msctf.dll` and `ntdll.dll`). Without native exports in those DLLs, the final link fails with errors like `undefined symbol: TF_CreateCategoryMgr (native symbol)`.

Bazel's patcher cannot apply a second patch to a file that an earlier patch already modified (`CONTENT_DOES_NOT_MATCH_TAR`). These changes are therefore part of the existing per-file rules_cc patches rather than a separate patch file.

### Platform

`@platforms` has no CPU value for ARM64EC or ARM64X. ARM64X is modeled as `cpu:arm64` plus a Mozc-specific constraint in `src/BUILD.bazel`.

```python
constraint_setting(
    name = "windows_arm64x_setting",
    visibility = ["//visibility:public"],
)

constraint_value(
    name = "arm64x",
    constraint_setting = ":windows_arm64x_setting",
    visibility = ["//visibility:public"],
)

platform(
    name = "windows-arm64x",
    constraint_values = [
        "@platforms//cpu:arm64",
        "@platforms//os:windows",
        ":arm64x",
    ],
)
```

The constraint needs public visibility because the generated `@local_config_cc` repository refers to it.

The arm64 toolchain also matches `//:windows-arm64x`. When multiple registered toolchains match, the one listed later in `--extra_toolchains` wins, so the ARM64X toolchain is registered last in `src/.bazelrc`.

```
build:windows_env --extra_toolchains=@local_config_cc//:cc-toolchain-arm64_windows-clang-cl
build:windows_env --extra_toolchains=@local_config_cc//:cc-toolchain-x64_windows-clang-cl
build:windows_env --extra_toolchains=@local_config_cc//:cc-toolchain-x64_x86_windows-clang-cl
# Must come after the arm64 toolchain, as later --extra_toolchains take precedence.
build:windows_env --extra_toolchains=@local_config_cc//:cc-toolchain-arm64x_windows-clang-cl
```

`--toolchain_resolution_debug='@bazel_tools//tools/cpp:toolchain_type'` shows which toolchain is selected.

Because both halves are compiled in a single configuration, `select()` sees `cpu:arm64` for both of them. Anything that needs to differ between the ARM64 and ARM64EC halves must use the preprocessor (`_M_ARM64EC`), not `select()`.

### Third-party code

For ARM64EC, clang predefines `__x86_64__`, `__amd64__`, `_M_X64`, `_M_AMD64` and `__GCC_ASM_FLAG_OUTPUTS__`, in addition to `__arm64ec__` and `_M_ARM64EC`. It does not define `__SSE__` or `__aarch64__`. Code that selects x86 inline assembly by `__x86_64__` alone fails to assemble, because the generated code is AArch64.

In the dependency tree of the TIP, there are three such places, all in third-party code. Mozc's own code needs no changes. They are fixed by excluding `_M_ARM64EC`, the macro abseil already uses elsewhere, with patches applied through `single_version_override` (`src/bazel/abseil-cpp_arm64ec.patch` and `src/bazel/protobuf_arm64ec.patch`). These patches need `diff --git` headers, because Bazel's patcher misassigns hunks in multi-file patches without them.

abseil-cpp, `absl/base/internal/unscaledcycleclock_config.h` (`rdtsc`). abseil already excludes ARM64EC here for MSVC, but under clang the `__x86_64__` check matches first.

```diff
-#if defined(__i386__) || defined(__x86_64__) || defined(__aarch64__) || \
-    defined(__powerpc__) || defined(__ppc__) || defined(_M_IX86) ||     \
-    (defined(_M_X64) && !defined(_M_ARM64EC))
+#if (defined(__i386__) || defined(__x86_64__) || defined(__aarch64__) || \
+     defined(__powerpc__) || defined(__ppc__) || defined(_M_IX86) ||     \
+     defined(_M_X64)) &&                                                 \
+    !defined(_M_ARM64EC)
 #define ABSL_HAVE_UNSCALED_CYCLECLOCK_IMPLEMENTATION 1
```

abseil-cpp, `absl/base/prefetch.h` (`prefetchw`). This falls back to `__builtin_prefetch`.

```diff
-#if defined(__x86_64__) && !defined(__PRFCHW__)
+#if defined(__x86_64__) && !defined(__PRFCHW__) && !defined(_M_ARM64EC)
   asm("prefetchw %0" : : "m"(*reinterpret_cast<const char*>(addr)));
```

protobuf, `src/google/protobuf/varint_shuffle.h` (`btc`). This falls back to the existing portable XOR.

```diff
-#if defined(__GCC_ASM_FLAG_OUTPUTS__) && defined(__x86_64__)
+#if defined(__GCC_ASM_FLAG_OUTPUTS__) && defined(__x86_64__) && \
+    !defined(_M_ARM64EC)
     asm("btc %[bits], %[res3]" : [res3] "+r"(res3) : [bits] "i"(bits));
```

To find such issues, a temporary toolchain for ARM64EC alone (`--target=arm64ec-pc-windows-msvc`) can be used to build the dependencies with `--keep_going`, which collects all failures at once.

### TIP target

`mozc_tip64x` is an ordinary production binary for `//:windows-arm64x` in `src/win32/tip/BUILD.bazel`. It replaces both the `mozc_tip64arm` target and the forwarder genrule, and `build_tip_forwarder_dll.py` is removed.

```python
mozc_win32_cc_prod_binary(
    name = "mozc_tip64x",
    srcs = ["mozc_tip_main.cc"],
    executable_name_map = {
        "Mozc": "mozc_tip64x.dll",
        "GoogleJapaneseInput": "GoogleIMEJaTIP64X.dll",
    },
    linkshared = True,
    platform = "//:windows-arm64x",
    static_crt = True,
    win_def_file = "mozc_tip.def",
    deps = [":mozc_tip_deps"],
    ...
)
```

`mozc_win32_cc_prod_binary` already applies a platform transition, so nothing else is needed. Resources are compiled for `cpu:arm64`, so the version resource names under `_ARM64_` in `tip_resource.rc` are `mozc_tip64x` / `GoogleIMEJaTIP64X`.

### Installer and custom action

The installer that contains ARM64 binaries is used on both x64 and ARM64 Windows. It installs `mozc_tip64.dll` only on x64 Windows and `mozc_tip64x.dll` only on ARM64 Windows.

```xml
<Component Id="MozcTIP64" Permanent="no" Bitness="always64" Condition="NOT (PROCESSOR_ARCHITECTURE=&quot;ARM64&quot;)">
  ...
</Component>
<Component Id="MozcTIP64X" Permanent="no" Bitness="always64" Condition="PROCESSOR_ARCHITECTURE=&quot;ARM64&quot;">
  ...
</Component>
```

`PROCESSOR_ARCHITECTURE` is a secure property read from the registry. It used to be defined only in the x64-only installer for its launch condition, and is now always defined. The x64-only installer keeps its layout.

The custom action selects the 64-bit TIP DLL with `GetMozcTIP64Path()`: `mozc_tip64x.dll` on ARM64 Windows and `mozc_tip64.dll` otherwise. It is used both to register the COM server and to grant `ALL APPLICATION PACKAGES` access to the DLL.

## Verification

 * `llvm-readobj --file-headers --coff-exports --coff-load-config` on the DLL shows `Format: COFF-ARM64X`, and `DllGetClassObject` / `DllCanUnloadNow` in both the native view and the hybrid (ARM64EC) view.
 * A test program calls `DllGetClassObject` with the TIP's CLSID, then `IClassFactory::CreateInstance(IID_ITfTextInputProcessor)` and `DllCanUnloadNow`. Built as ARM64 and as x64 and run on ARM64 Windows, every call returns `S_OK` in both.
 * The MSI contains `mozc_tip32.dll`, `mozc_tip64.dll` and `mozc_tip64x.dll`, with the component conditions above.

Note that the new target writes into the transitioned `bazel-out/...-ST-<hash>/` directory. An old `bazel-bin/win32/tip/mozc_tip64x.dll` left over from the previous genrule can be mistaken for the new DLL.

## Size

Comparison of the ARM64X TIP DLL with the standalone ARM64 and x64 TIP DLLs (bytes):

| Section | ARM64X | ARM64 only | x64 only | Shared? |
|---|---|---|---|---|
| `.text` | 3,530,822 | 1,672,196 | 1,772,294 | no (ARM64 1,672,196 + ARM64EC 1,852,936 + x64 thunks 4,144) |
| `.rdata` | 862,120 | 506,072 | 520,880 | partly (identical constants are folded by `/OPT:ICF`) |
| `.data` | 314,740 | 156,952 | 156,936 | no |
| `.rsrc` | 2,626,408 | 2,626,416 | 2,626,408 | yes |
| file | 7,320,064 | 4,950,016 | 5,094,912 | |

Code is never shared, as ARM64EC follows the x64 calling convention. Apart from resources, the ARM64X DLL is about as large as the two standalone DLLs combined. The benefits are one TIP DLL per ARM64 machine and no forwarding at load time, not size.

## Alternatives considered

### Keep the pure forwarder with `/DEPENDENTLOADFLAG:0x100`

[`/DEPENDENTLOADFLAG`](https://learn.microsoft.com/en-us/cpp/build/reference/dependentloadflag) also controls how forwarded exports are resolved. With `0x100` (`LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR`), the forward target is always looked up in the forwarder's directory, no matter how the forwarder is loaded.

For an ARM64X image, however, `DependentLoadFlags` exists in each view's load config. MSVC `link.exe` 14.51 sets it only for the ARM64 view, so emulated x64 processes stay affected. `lld-link` 23.1.6 sets it for both views. This is a smaller change and was considered as a short-term fix. The full ARM64X DLL removes the forwarding altogether, and also removes two DLLs from ARM64 installations.

### Model ARM64X differently in Bazel

 * Use a custom constraint value for `@platforms//cpu:cpu`. This makes toolchain resolution unambiguous, but `select()` on `@platforms//cpu:arm64` / `aarch64` in Mozc and its dependencies would no longer match, and each such `select()` would need to be audited.
 * Use a rules_cc feature on the arm64 toolchain, enabled by a transition in `mozc_win32_cc_prod_binary`. This avoids a new toolchain and the registration order, but features are meant for optional toggles rather than target differences, they are not easily visible to `select()`, and the toolchain patch would be more invasive.
 * Bazel has no way for a target to choose a C++ toolchain directly. Toolchains are resolved from the target platform, so a "toolchain" parameter on `mozc_win32_cc_prod_binary` would just be an alias for a platform.

## Future work

 * The dependency on the `--extra_toolchains` order can be removed by giving `windows_arm64x_setting` a default value (e.g. `:not_arm64x`) and requiring it in the arm64 toolchain.
 * rules_cc is moving toward rule-based toolchains (`cc_toolchain`, `cc_args`, `cc_feature`), where a single toolchain supports multiple platforms through `select()` on constraints. MSVC and clang-cl support for rule-based toolchains is still in progress upstream. Once it is available, the ARM64X support could become a few `cc_args` (`-marm64x`, `/MACHINE:ARM64X`, `/defArm64Native`) selected on `//:arm64x` in the same clang-cl toolchain as arm64, without a separate toolchain. The `//:windows-arm64x` platform and the `mozc_tip64x` target would not need to change.
 * [hermeticbuild/hermetic-llvm](https://github.com/hermeticbuild/hermetic-llvm) could replace the auto-configured Windows toolchain of rules_cc altogether. It is a fully hermetic, zero-sysroot LLVM `cc_toolchain` for Bazel, built on the rules_cc toolchain API, and already supports MSVC-ABI targets with clang-cl, including `aarch64-windows-msvc` and `x86_64-windows-msvc` from both x86_64 and aarch64 Windows hosts. This would remove the need to patch rules_cc and to depend on a locally installed LLVM. As of this writing, it does not list 32-bit x86 Windows targets, which Mozc needs for the 32-bit TIP DLL, nor ARM64EC / ARM64X targets, so these would need to be added there first.
 * The `/Fo` and response-file issues in clang-cl, and the x86 inline assembly guarded only by `__x86_64__` in abseil and protobuf, could be reported upstream.
 * Building on x64 hosts, the universal installer configuration, and the Google Japanese Input branding have not been verified yet.

## References

 * Arm64X build guide: https://learn.microsoft.com/en-us/windows/arm/arm64x-build
 * `/DEPENDENTLOADFLAG`: https://learn.microsoft.com/en-us/cpp/build/reference/dependentloadflag
 * `-Wl,--native-def` for ARM64X DLLs in llvm-mingw: https://github.com/mstorsjo/llvm-mingw/issues/581#issuecomment-5979953121
 * Multi-architecture COFF objects for ARM64X (LLVM Discourse): https://discourse.llvm.org/t/multi-architecture-coff-object-files-for-arm64x-using-an-extra-section/91030
 * rules_cc toolchain API: https://github.com/bazelbuild/rules_cc/blob/main/docs/toolchain_api.md
 * hermeticbuild/hermetic-llvm: https://github.com/hermeticbuild/hermetic-llvm
