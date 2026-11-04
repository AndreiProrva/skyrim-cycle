# Corresponding source and developer build

The source ZIP contains the complete native plugin source and the exact local
CommonLib source snapshot used by the approved binary, plus source snapshots
of linked/header dependencies. Public source is a clean export, not the private
working repository/history. The runtime ZIP supplies the binary pose data,
models/textures and audio needed to play. Blender source assets and the original
game skeleton are not required to compile the native DLL and are not licensed
as part of the code archive.

Toolchain used: MSVC 14.44.35207 / Visual Studio 2022, Windows x64 SDK,
C++ latest mode, Release /O2, /MD, static CommonLib/fmt/spdlog/DirectXTK.
Dependency versions: CommonLib local 12.1.0 snapshot with 1.7.104 additions;
fmt 12.1.0; spdlog 1.16.0; DirectXTK 2025-10-27;
DirectXMath 2025-04-03; rapidcsv 8.90. The local CommonLib build used
SKSE_SUPPORT_PATCH_SAFETY=OFF; no hde64 decoder was linked.

## Prepare dependencies

Use CMake and vcpkg with the x64-windows-static-md triplet to build/install
fmt, spdlog, directxtk and rapidcsv (including DirectXMath). Source snapshots
are included under dependencies for inspection and reconstruction at the
versions above. The vcpkg tool and a Windows compiler/SDK are external build
tools. The included CommonLib vcpkg manifest describes its own full dependency
set; optional test/config features can be disabled for this plugin build.

Build the supplied `dependencies/CommonLibSSE` from source with CMake,
the vcpkg toolchain and the same triplet. Enable SE/AE support, disable tests,
and use SKSE_SUPPORT_PATCH_SAFETY=OFF to match the archived library build.
The .git release metadata is intentionally absent, so automatic clean-tag
prebuilt fetching does not apply; leave COMMONLIB_PREBUILT_DIR empty.

Example from the extracted source root, with your vcpkg installation path:

    cmake -S dependencies/CommonLibSSE -B build-commonlib -DCMAKE_TOOLCHAIN_FILE="<vcpkg>/scripts/buildsystems/vcpkg.cmake" -DVCPKG_TARGET_TRIPLET=x64-windows-static-md -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL -DBUILD_TESTS=OFF -DSKSE_SUPPORT_PATCH_SAFETY=OFF -DENABLE_SKYRIM_VR=OFF -DCOMMONLIB_ENABLE_IPO=OFF
    cmake --build build-commonlib --config Release

Resolve CommonLibSSE.lib from the resulting Release directory and the installed
vcpkg prefix for that build. From an **x64 VS2022 Developer Command Prompt**:

    build-native.cmd dependencies\CommonLibSSE "<path-to-built>\CommonLibSSE.lib" "<vcpkg-installed>\x64-windows-static-md"

The script takes all dependency paths as arguments and runs MotionTests before
building `build/C3KeyboardMotion.dll`. It does not install anything into a game.
Copy a development build into a separate test profile only when Skyrim is closed.
Do not replace the archived approved binary as part of publication preparation.

This source build can differ byte-for-byte with toolchain timestamps, paths,
dependency builds or compiler versions. A successful compilation does not
establish in-game compatibility; the distributed runtime is the user-approved
binary. Native plugin support remains restricted to Skyrim 1.7.104 / SKSE 2.3.1.

See dependencies/CommonLibSSE/COPYING.txt and EXCEPTIONS.md for its current
license and linking/modding permissions. Other dependency licenses are preserved
alongside their source and in licenses/. Code licensing does not extend rights
over Bethesda game data or the separately licensed bicycle visual assets.
