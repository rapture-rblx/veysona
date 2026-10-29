# Optional Windows ASIO build — v1.28

The ASIO backend is OFF by default. The source never bundles Steinberg's ASIO SDK or any audio-interface driver.

For a **proprietary** Veysona release, obtain the appropriate Steinberg ASIO SDK agreement and headers. The proprietary licence is royalty-free, but accepting/signing the licence and satisfying its requirements may still be necessary.

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
  -DVEYSONA_BUILD_GUI=ON -DVEYSONA_ENABLE_ASIO=ON `
  -DVEYSONA_ASIO_SDK_INCLUDE_DIR="C:/path/to/asiosdk/common"
cmake --build build --config Release --parallel 4
ctest --test-dir build -C Release --output-on-failure
```

For open-source builds, verify compatibility of JUCE, ASIO and the project's **actual** licensing terms before distributing binaries. An open Git repository alone is not sufficient.
