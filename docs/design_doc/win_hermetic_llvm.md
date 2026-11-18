# Building Mozc for Windows with hermetic-llvm

## Objective

Build the x64 and ARM64 Windows binaries of Mozc with [hermeticbuild/hermetic-llvm](https://github.com/hermeticbuild/hermetic-llvm), so that the compiler, the linker, the MSVC runtime and the Windows SDK are pinned in `MODULE.bazel` rather than taken from whatever is installed on the build machine.

This is an experiment. It shows how small the required change is and what still prevents a fully hermetic Windows build: 32-bit x86 targets and ATL.

## Background

Mozc for Windows has been built with the clang-cl toolchain that rules_cc configures automatically (`@local_config_cc`).

 * `clang-cl.exe`, `lld-link.exe` and `llvm-lib.exe` come from the LLVM archive that `build_tools/update_deps.py` extracts into `third_party/llvm`.
 * The MSVC headers and libraries (CRT, STL and ATL) and the Windows SDK come from the local Visual Studio, which `src/bazel/bazel_wrapper/bazel.bat` locates and passes as `BAZEL_VC`. rules_cc picks the newest toolset in it unless `BAZEL_VC_FULL_VERSION` is set.
 * rules_cc is patched (`src/bazel/rules_cc_*.patch`) to add a clang-cl toolchain for 32-bit x86, which upstream does not have.

Only the LLVM version is pinned by the Mozc repository. The rest depends on the machine, which has two costs.

 * A Visual Studio update can break the build without any change in the repository. Visual Studio 2026 with MSVC 14.52 is an example: its STL requires Clang 22 or newer (`error STL1000: Unexpected compiler version, expected Clang 22 or newer`), while `third_party/llvm` had LLVM 20.1.1. The build stayed broken until LLVM was updated to 23.1.6.
 * The same commit can produce different binaries on different machines, and developers and CI need to install and maintain Visual Studio.

hermetic-llvm is a `cc_toolchain` for Bazel built on the rule-based toolchain API of rules_cc. It downloads a prebuilt LLVM and everything else that the target needs, and has supported MSVC-ABI targets (`x86_64-pc-windows-msvc` and `aarch64-pc-windows-msvc`) with clang-cl and lld-link since August 2026. For these targets, the MSVC runtime and the Windows SDK are downloaded from Microsoft by the [windows_support](https://github.com/hermeticbuild/windows_support) module.

## Design overview

x64 and ARM64 targets, including the tools built for the host, are built with hermetic-llvm and Microsoft STL. 32-bit x86 targets, which means `mozc_tip32.dll` and its dependencies, keep using the clang-cl toolchain in `@local_config_cc`, because hermetic-llvm has no 32-bit x86 Windows target.

| Component | Before | After |
|---|---|---|
| clang-cl, lld-link, llvm-lib (x64, ARM64) | `third_party/llvm` (23.1.6) | hermetic-llvm 0.8.27 (LLVM 23.1.2) |
| CRT and STL (x64, ARM64) | local Visual Studio | downloaded MSVC 14.50.35717 |
| Windows SDK headers and libraries (x64, ARM64) | local Windows SDK | downloaded 10.0.26100.7705 |
| ATL (x64, ARM64) | local Visual Studio, through `INCLUDE` / `LIB` | local Visual Studio, through `@atl//:atl` |
| Everything for 32-bit x86 | `@local_config_cc` | unchanged |
| `rc.exe`, CRT redistributable DLLs, ARM64X forwarder, Qt, WiX | local | unchanged |

No C++ source file needs to change. The change consists of build configuration only, split into the following commits.

1. Update rules_cc to 0.2.25, which hermetic-llvm requires.
2. Disable the opposite CRT feature in `_win_executable_transition`.
3. Pass `/OPT:REF` and `/OPT:ICF` to production binaries explicitly.
4. Add the `@atl` repository.
5. Add hermetic-llvm and use it for x64 and ARM64.

Commits 1 to 4 do not change which toolchain is used. The installer was built with `@local_config_cc` after commit 4 to confirm that they work without hermetic-llvm.

## Detailed design

### Module and license acceptance

```python
bazel_dep(
    name = "llvm",
    version = "0.8.27",
)
```

hermetic-llvm 0.8.27 uses LLVM 23.1.2 and configures windows_support to download MSVC 14.50.35717 and Windows SDK 10.0.26100.7705. They are downloaded lazily, only when an MSVC-ABI target is built, and only if the following options are specified. With them, the user declares to accept the license terms of Visual Studio and the Windows SDK.

```
--repo_env=BAZEL_MSVC_RUNTIME_VISUAL_STUDIO_EULA=1
--repo_env=BAZEL_WINDOWS_SDK_EULA=1
```

These options are intentionally not written in `src/.bazelrc`. Accepting the licenses is a decision of whoever runs the build. Without them, Windows builds fail when the `@msvc_runtime` repository is fetched.

In the Bazel output base, the LLVM archive takes about 170 MiB, the Windows SDK about 850 MiB, and the MSVC runtime about 2.1 GiB, half of which is the downloaded archives.

### Platforms

By default, hermetic-llvm builds Windows targets for the MinGW ABI with libc++. The MSVC ABI and Microsoft STL are selected with constraints on the target platform. They are added to all the Windows platforms in `src/BUILD.bazel` except `//:windows-x86_32`.

```python
HERMETIC_LLVM_MSVC_CONSTRAINTS = [
    "@llvm//constraints/cxxstdlib:msvcstl",
    "@llvm//constraints/windows/abi:msvc",
    "@llvm//constraints/windows/crt:ucrt",
]

platform(
    name = "host-windows-clang-cl",
    constraint_values = [
        "@platforms//os:windows",
        "@bazel_tools//tools/cpp:clang-cl",
    ] + HERMETIC_LLVM_MSVC_CONSTRAINTS,
    parents = ["@platforms//host"],
)
```

Microsoft STL is required rather than libc++, which is the default even for the MSVC ABI, because `mozc_tool.exe` links to the Qt libraries in `third_party/qt`. They are built with MSVC by `build_tools/build_qt.py`, and Qt and Mozc need to agree on the C++ standard library.

hermetic-llvm reports `clang-cl` as the compiler for the MSVC ABI, so `//:compiler_msvc_like` and all the MSVC-style options in `src/.bazelrc` (`/std:c++20`, `/J`, `/utf-8` and so on) keep working as they are.

### Toolchain registration

```
build:windows_env --extra_toolchains=@llvm//toolchain:all
build:windows_env --extra_toolchains=@local_config_cc//:cc-toolchain-x64_x86_windows-clang-cl
```

`@llvm//toolchain:all` replaces the x64 and ARM64 clang-cl toolchains of `@local_config_cc`. Its toolchains require `@platforms//cpu:x86_64` or `aarch64`, so `//:windows-x86_32` can only match the remaining `@local_config_cc` toolchain.

Both x64 and ARM64 Windows are supported as the execution platform. Only x64 hosts were verified.

### CRT linkage

`mozc_win32_cc_prod_binary` selects the CRT linkage by rewriting `--features` in a transition. It used to add the wanted feature and remove the other one from the list.

Removing a name from `--features` does not disable a feature that the toolchain enables by default. In hermetic-llvm, `dynamic_link_msvcrt` is enabled by default and is mutually exclusive with `static_link_msvcrt`, so analysis failed for the targets with `static_crt = True`.

```
Error in configure_features: Symbol @@llvm+//toolchain/features/msvc:crt_mode is provided by all of the following features: dynamic_link_msvcrt static_link_msvcrt
```

The transition now also disables the other feature explicitly.

```python
if attr.static_crt:
    features = _remove_if_present(features, "dynamic_link_msvcrt")
    features = _remove_if_present(features, "-static_link_msvcrt")
    features = _append_if_absent(features, "-dynamic_link_msvcrt")
    features = _append_if_absent(features, "static_link_msvcrt")
```

hermetic-llvm chooses the CRT library directory with a `config_setting` on `--features`, so the CRT linkage has to be set on the command line or by a transition like this one. The `features` attribute of a target is not enough.

### `/OPT:REF` and `/OPT:ICF`

`mozc_win32_cc_prod_binary` always passes `/DEBUG:FULL`, which changes the linker defaults to `/OPT:NOREF` and `/OPT:NOICF`. The toolchain in `@local_config_cc` passes `/OPT:REF` and `/OPT:ICF` for `--compilation_mode=opt`, but hermetic-llvm does not. `mozc_win32_cc_prod_binary` now passes them by itself.

| | `mozc_tip64.dll` | `Mozc64.msi` (universal) |
|---|---|---|
| hermetic-llvm, as is | 7,216,640 | 33,239,040 |
| hermetic-llvm, with `/OPT:REF` and `/OPT:ICF` | 5,094,912 | 27,475,968 |

### ATL

The MSVC payload that windows_support downloads consists of the `include` and `lib` directories of the toolset. ATL (`atlmfc`) is a separate Visual Studio component and is not included. 19 files in Mozc include ATL headers.

| ATL type | Occurrences | Header |
|---|---|---|
| `CRect`, `CPoint`, `CSize` | 157 | `atltypes.h` |
| `CRegKey` | 16 | `atlbase.h` |
| `CWindowImpl`, `CWindow`, `BEGIN_MSG_MAP` | 14 | `atlwin.h` |
| `CStringW`, `CString` | 6 | `atlstr.h` |
| `CSid`, `CDacl` | 3 | `atlsecurity.h` |

As a stopgap, `src/bazel/atl_repository.bzl` creates `@atl`, which links `atlmfc/include` and `atlmfc/lib` of the local Visual Studio into a repository, and `//:macro` depends on `@atl//:atl`. Every target defined with `mozc_cc_library`, `mozc_cc_binary` or `mozc_cc_test` depends on `//:macro`, which keeps ATL available everywhere as before.

```python
cc_library(
    name = "atl_impl",
    hdrs = glob(["include/**"]),
    includes = ["include"],
    # ATL headers request "atls.lib" with '#pragma comment(lib, ...)'.
    additional_linker_inputs = select({
        "@platforms//cpu:arm64": glob(["lib/arm64/*.lib"]),
        "@platforms//cpu:x86_64": glob(["lib/x64/*.lib"]),
    }),
    linkopts = select({
        "@platforms//cpu:arm64": ["/LIBPATH:external/{repo_name}/lib/arm64"],
        "@platforms//cpu:x86_64": ["/LIBPATH:external/{repo_name}/lib/x64"],
    }),
)
```

 * The library directory is passed as a path relative to the execution root. The absolute path contains spaces (`C:\Program Files\...`), and Bazel tokenizes `linkopts` like a shell would.
 * `@atl//:atl` is empty for `@platforms//cpu:x86_32`, where the toolchain in `@local_config_cc` already provides ATL.
 * The ATL version follows the local Visual Studio (or `BAZEL_VC_FULL_VERSION`), while the CRT and STL are 14.50. ATL 14.44 and 14.52 were both verified to work with them.

This is the one place where an x64 or ARM64 binary still depends on the local Visual Studio.

### rules_cc

hermetic-llvm requires rules_cc 0.2.25 or newer, while Mozc pinned 0.2.22 with `single_version_override`. The three patches for the x86 clang-cl toolchain are regenerated, because Bazel's patcher does not accept patches that only apply with fuzz. They are still needed for `mozc_tip32.dll`.

Both steps also raise the resolved versions of other modules for all platforms. rules_cc 0.2.25 raises apple_support from 2.4.0 to 2.8.0. hermetic-llvm raises platforms from 1.0.0 to 1.1.0, rules_go from 0.60.0 to 0.62.0, gazelle from 0.48.0 to 0.51.3, rules_shell from 0.6.1 to 0.8.0 and bazel_lib from 3.0.0 to 3.2.2.

### What works without changes

hermetic-llvm links through `clang-cl` instead of running `lld-link` directly, and forwards `linkopts` to the linker one by one.

```
/clang:-Xlinker /clang:/SUBSYSTEM:WINDOWS
/clang:-Xlinker /clang:/MANIFESTINPUT:server/mozc_server.exe.manifest
```

Therefore the MSVC linker options that Mozc passes work as they are. This includes `/SUBSYSTEM`, `/DEBUG:FULL`, `/PDBALTPATH`, `/DEPENDENTLOADFLAG`, `/CETCOMPAT`, `/MANIFEST:EMBED`, `/MANIFESTINPUT`, the `.res` files from `mozc_win32_resource`, and the system libraries such as `user32.lib` from `mozc_win32_lib`. `win_def_file`, the `interface_library` output group used by `mozc_cc_win32_library`, and `generate_pdb_file` with its `pdb_file` output group are also supported.

The command lines differ from the ones of `@local_config_cc` in the following points.

| | `@local_config_cc` | hermetic-llvm |
|---|---|---|
| Warnings | clang-cl defaults, with `/wd4996` | `-Wall -Wthread-safety -Wself-assign` |
| `_WIN32_WINNT` | `0x0A00` for all code, from `BAZEL_WIN32_WINNT` | not defined by the toolchain (Mozc targets get it from `//:macro`) |
| Reproducibility | | `/Brepro`, redacted `__DATE__` and `__TIME__`, `/pdbsourcepath:.` |
| Subsystem of DLLs | console | Windows GUI (the linker default) |
| `--compilation_mode=dbg` | `/MDd` or `/MTd` | `/MD` or `/MT` |

## Verification

All of the following were run on an x64 Windows host at the last commit, with Visual Studio 2026 (MSVC 14.52.37025) and LLVM 23.1.6 in `third_party/llvm` for the 32-bit x86 toolchain.

| Command | Result |
|---|---|
| `bazelisk build package` | success |
| `bazelisk build package --config release_build --config win_universal_installer` | success |
| `bazelisk test ... -c dbg --build_tests_only` | 231 of 236 tests passed, 5 skipped |

The 5 skipped tests are `//renderer/qt:qt_build_test` and four tests in `//unix/ibus`, which are compatible only with Linux.

`llvm-readobj` on the built binaries shows the following.

 * `mozc_server.exe`, `mozc_tool.exe` and the other executables import `VCRUNTIME140.dll`, `VCRUNTIME140_1.dll` and `MSVCP140.dll`. `mozc_tool.exe` also imports `Qt6Core.dll`, `Qt6Gui.dll` and `Qt6Widgets.dll`.
 * `mozc_tip64.dll` and `mozc_installer_helper.dll` import no CRT DLL.
 * `mozc_tip64arm.dll`, cross-compiled on x64, is an ARM64 binary.
 * The manifest, the version resource, the exports from `mozc_tip.def`, `DependentLoadFlags`, CET compatibility and the PDB file name are present as before.

The installer was not installed, and the binaries were not run other than through the tests.

## Size

Release binaries of the same source, in bytes. The local toolchain is clang-cl 23.1.6 with MSVC 14.52.37025.

| Binary | `@local_config_cc` | hermetic-llvm |
|---|---|---|
| `mozc_server.exe` | 22,719,488 | 22,732,800 |
| `mozc_tip64.dll` | 5,097,984 | 5,094,912 |
| `mozc_tip64arm.dll` | 4,952,576 | 4,934,656 |
| `mozc_tool.exe` | 3,155,456 | 3,163,648 |
| `mozc_installer_helper.dll` | 2,295,296 | 2,291,200 |
| `mozc_broker.exe` | 2,038,272 | 2,044,416 |
| `mozc_renderer.exe` | 1,988,096 | 1,994,240 |
| `mozc_cache_service.exe` | 1,446,912 | 1,448,960 |
| `mozc_tip32.dll` | 4,798,976 | 4,798,976 (`@local_config_cc`) |

The difference is within 0.4%. The compiler and the CRT are different versions, so this only shows that nothing is grossly different. Performance was not compared.

## Known issues

 * **Debug CRT.** hermetic-llvm does not support `/MDd` and `/MTd`. With `--compilation_mode=dbg`, Mozc code is built for the retail CRT while `@qt_win` selects the debug Qt libraries, so `mozc_tool.exe` loads `MSVCP140.dll` together with `Qt6Cored.dll`, which uses the debug CRT. It links, but two CRTs in one process is not a supported configuration. `@qt_win` should select the release libraries until the debug CRT is supported.
 * **Warnings.** `/wd4996` is no longer passed, which reveals about 6,000 `-Wdeprecated-declarations` warnings for `[[deprecated]]` declarations in Mozc itself. They come on top of about 30,000 `-Wdeprecated-attributes` warnings for `lifetime_capture_by(this)` in the headers of abseil-cpp 20260107.1, which Clang 23 emits with either toolchain.
 * **Mixed toolsets.** The 32-bit TIP is built with the local toolset and the 64-bit TIPs with MSVC 14.50. This is fine as they are never loaded into the same process, but the two can now drift apart.
 * **CRT DLLs in the installer.** `build_installer.py` still copies the CRT redistributable DLLs from the local Visual Studio. They must not be older than the toolset used to build the executables, which is now 14.50 regardless of the local one.
 * **Maturity.** MSVC-ABI support in hermetic-llvm is a few months old and still changing. Microsoft STL works, but is tracked as an open issue upstream and its support may change.
 * **Other platforms.** The module version changes listed above also apply to Linux, macOS and Android builds, which were not verified.

## Alternatives considered

### Keep `@local_config_cc` and update LLVM

Updating `third_party/llvm` to LLVM 23.1.6 fixed the build with MSVC 14.52 with a much smaller change. It does not pin the MSVC runtime or the Windows SDK, so the next incompatible Visual Studio update needs the same kind of fix again. The local LLVM is still required for the 32-bit TIP, and does not conflict with this design.

### libc++ instead of Microsoft STL

libc++ is the default of hermetic-llvm and the better tested configuration there, and it is linked statically, which would remove the dependency on `MSVCP140.dll`. It cannot be used as long as Qt is built with MSVC, and it would make Mozc for Windows differ from what Visual Studio users build.

### MinGW ABI

This is the most mature Windows target of hermetic-llvm and needs no Microsoft license. Mozc relies on ATL, WIL and Qt built for the MSVC ABI, so it is not an option.

### Make hermetic-llvm opt-in

The toolchains could be registered under a separate config (e.g. `--config=hermetic_llvm`) rather than `windows_env`, keeping `@local_config_cc` as the default. The platform constraints are ignored by `@local_config_cc`, so only the `--extra_toolchains` lines would move. This avoids requiring the license options for every Windows build, and is the safer choice if this is merged before the remaining issues are resolved.

### Register only the MSVC toolchains

`@llvm//toolchain:all` also registers the toolchains for other targets. The four MSVC toolchains can be listed individually (`@llvm//toolchain:windows_x86_64_to_windows_x86_64_msvc` and so on), or through the `toolchain` module extension of hermetic-llvm. `:all` is used for simplicity, as other targets never match the Windows platforms of Mozc.

## Future work

 * **32-bit x86.** Neither hermetic-llvm nor windows_support knows 32-bit x86 for the MSVC ABI. windows_support would need the `Microsoft.Windows.SDK.CPP.x86` package and the `lib/x86` targets of the MSVC runtime, and hermetic-llvm would need the `i686-pc-windows-msvc` target with its platform settings and runtime libraries. Once this is available, the rules_cc patches, `third_party/llvm` and the Visual Studio requirement can be removed.
 * **ATL.** Either add the ATL component to windows_support, or remove ATL from Mozc. Most of the uses are simple geometry types and registry access, and the rest is the window classes of the renderer.
 * **ARM64EC and ARM64X.** `mozc_tip64x.dll` is built by `build_tip_forwarder_dll.py` with `cl.exe` and `link.exe` of the local Visual Studio. hermetic-llvm has no ARM64EC or ARM64X target, which would also be needed to build a full ARM64X TIP DLL with it.
 * **Resource compiler.** `mozc_win32_resource` runs `rc.exe` of the local Windows SDK, found through the registry. The LLVM archive of hermetic-llvm contains `llvm-rc.exe`, and the SDK headers are already downloaded.
 * **CRT redistributable DLLs.** `@msvc_runtime//:vc_redist` provides the DLLs that match the downloaded toolset. Using it in the installer needs windows_support as a direct dependency of Mozc.
 * **Versions.** The versions of the MSVC runtime and the Windows SDK are chosen by hermetic-llvm. windows_support lets the root module override them, but hermetic-llvm also hard-codes the matching `-fms-compatibility-version`, so this was not tried.
 * **CI.** GitHub Actions could cache the downloaded repositories, and the test jobs, which do not build `mozc_tip32.dll`, would no longer need LLVM from `update_deps.py`, though they still need Visual Studio for ATL. hermetic-llvm can also cross-compile MSVC-ABI targets on Linux and macOS, but Mozc's resource and installer rules only work on Windows.

## References

 * hermeticbuild/hermetic-llvm: https://github.com/hermeticbuild/hermetic-llvm
 * hermeticbuild/windows_support: https://github.com/hermeticbuild/windows_support
 * Native MSVC ABI targets in hermetic-llvm: https://github.com/hermeticbuild/hermetic-llvm/issues/156
 * Microsoft STL for Windows MSVC targets in hermetic-llvm: https://github.com/hermeticbuild/hermetic-llvm/issues/878
 * Extended Windows support in hermetic-llvm: https://github.com/hermeticbuild/hermetic-llvm/issues/24
 * clang-cl toolchain for x86 in rules_cc: https://github.com/bazelbuild/rules_cc/pull/360
 * rules_cc toolchain API: https://github.com/bazelbuild/rules_cc/blob/main/docs/toolchain_api.md
 * Visual Studio license terms: https://visualstudio.microsoft.com/license-terms
