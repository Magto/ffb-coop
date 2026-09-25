# FFB Co-op

## Build and test

Windows only (MSVC via CMake, same toolchain as mewgenics-coop); WSL cannot build it.

- Configure: `cmake -B build -A x64`
- Build: `cmake --build build --config Release` → `build\Release\FFB Co-op.exe`
- Unit tests: `ctest --test-dir build -C Release --output-on-failure`
- List registered tests: `ctest --test-dir build -C Release -N`

The version lives in `src/ffb_version.h` only; `res/ffb_coop.rc` and the printed version line both read it.
New tests: one `tests/test_<name>.cpp` using `tests/ffb_test.h`, registered in `tests/CMakeLists.txt` with `add_test`.
