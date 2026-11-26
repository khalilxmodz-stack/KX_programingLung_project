# KX Studio Android — v0.2 development milestone

KX Studio is a native Android IDE shell for the KX v0.21 compiler, targeting **32-bit ARM (`armeabi-v7a`)**. The app uses Kotlin for the Android UI and C++17/JNI for the KX compiler frontend. APK builds are configured through GitHub Actions.

## Implemented in this milestone

- Native Android application project (Kotlin + C++17/JNI).
- Original KX v0.21 compiler headers integrated in the app's native source tree.
- In-app source editor with `.kx` open/save through Android document picker.
- Compile action calls `kx::Compiler::compileSource` directly.
- KX parser, semantic analysis, typed-IR validation, and LLVM IR emission run inside the app process.
- Compiler is configured for `armv7a-linux-androideabi24` and emits debug source-map entries.
- Diagnostics and generated LLVM IR are shown in the output panel.
- GitHub Actions workflow builds an ARMv7 debug APK.
- The Preview VM has been removed.

## Important: native execution is not implemented yet

This milestone **does not execute KX programs**. KX v0.21 emits LLVM IR, while its Build Manager invokes an external Clang process. This Android app does not yet bundle an in-process ARMv7 machine-code backend, an Android-compatible linker, or a native runtime loader. The Run button therefore reports this limitation instead of pretending to run code. There is no Preview VM or interpreter fallback.

The next milestone is to integrate and validate a real Android-compatible ARMv7 code-generation and loading path. The runtime must be bundled with the APK for offline use. A successful LLVM IR emission is not the same as a native executable or a successful Run.

## Build with GitHub Actions

1. Upload the contents of this folder to a GitHub repository's root. Keep `.github/workflows/android-apk.yml`.
2. Push to `main`/`master`, or open **Actions → Build KX Studio APK → Run workflow**.
3. Download the `KX-Studio-ARMv7-debug` artifact after the workflow succeeds.
4. Extract the artifact and install `app-debug.apk` on a compatible 32-bit ARM Android device.

## Local build requirements

- JDK 17
- Android SDK
- Android NDK `27.2.12479018`
- CMake `3.22.1`
- Gradle 8.9

Run `gradle --no-daemon assembleDebug` from the project root.

## Example source

```kx
fn main() -> i32 {
    i32 x = 40;
    i32 y = 2;
    print(x + y);
    return 0;
}
```

The app currently validates this program and emits LLVM IR; it does not yet print `42` by executing the program.
