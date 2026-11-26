# KX Studio Android — ARMv7 native toolchain

KX Studio targets 32-bit Android ARM (`armeabi-v7a`, API 24+) and uses Kotlin for the IDE, C++17/JNI for the KX frontend and native runtime.

## Toolchain components

- **KX frontend:** lexer, parser, semantic analysis, typed-IR validation and LLVM IR emission.
- **LLVM 18.1.8 ARM backend:** the CI pipeline cross-builds the ARM target and packages `libLLVM.so` for the Android app.
- **MCJIT:** the native Run path parses generated LLVM IR, initializes the ARM backend, JIT-compiles for the running ARMv7 process and calls the program entry point.
- **LLD:** CI builds the host linker and an Android ARMv7 linker binary. The binaries are staged as toolchain assets for the AOT linker integration milestone; they are **not yet invoked by the in-app Run action**.
- **Clang:** CI builds host and Android ARMv7 Clang and stages the Android executable and Clang resource headers. C/C++ compilation is a toolchain foundation; a user-facing C/C++ editor/compiler workflow is **not yet wired into the app**.

## Current limitations — read before installing

The LLVM/MCJIT path is under active integration and is not device-verified yet. The APK build must pass, then a real KX test program must run on an ARMv7 Android device before native execution can be called working.

The LLD and Clang executables are currently packaged as assets only. They are not yet extracted, launched, or connected to the KX AOT pipeline or C/C++ project UI. LLD-based standalone `libkx.so` output is therefore not implemented yet.

The Android runtime must also be checked for executable JIT memory permissions, LLVM symbol resolution, shared-library dependencies, and linker invocation restrictions.

## CI build

GitHub Actions builds LLVM 18.1.8, Clang and LLD for host/Android ARMv7, builds the Android APK, checks the expected ARMv7 libraries and toolchain assets, then uploads the APK artifact. No NDK installation is required on the developer's phone.

Open the repository's **Actions** tab and inspect **Build KX Studio ARMv7 APK with LLVM**. Only install an artifact after the run completes successfully.

## Device target

- ABI: `armeabi-v7a`
- Android minimum API: 24
- NDK: 27.2.12479018
- CMake: 3.22.1
- JDK: 17
- Gradle: 8.9

## KX smoke-test

```kx
fn main() -> i32 {
    i32 x = 40;
    i32 y = 2;
    print(x + y);
    return 0;
}
```

Expected output after a successful native JIT run: `42`.
