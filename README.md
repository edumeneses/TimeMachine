# TimeMachine

A spectral **freeze** audio plugin (phase-vocoder based), built with
[Avendish](https://github.com/celtera/avendish) from the
[Avendish processor template](https://github.com/celtera/avendish-audio-processor-template).

When *Freeze* is engaged the current spectral frame is held and resynthesised
indefinitely; *Dry/Wet* blends the frozen signal with the input.

## Download

Pre-built VST3 plugins for Windows, Linux and macOS are published as a rolling
"Continuous build" release. Grab the latest from the
[Releases page](../../releases/tag/continuous).

## Dependencies

The build fetches **Boost**, **pffft** and **Avendish** automatically
(`dependencies.cmake`). You additionally need the **ossia SDK** (provides the
VST3 SDK, pybind11 and libpd that the Avendish back-ends link against) and a
C++20 compiler.

Fetch the ossia SDK:

```bash
curl -L https://raw.githubusercontent.com/ossia/score/master/tools/fetch-sdk.sh > fetch-sdk.sh
chmod +x ./fetch-sdk.sh
./fetch-sdk.sh   # installs to /opt/ossia-sdk (Linux/macOS) or c:/ossia-sdk (Windows)
```

On Linux the system build packages are also needed, e.g. on Ubuntu:

```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake ninja-build \
    clang-17 lld-17 libc++-17-dev libc++abi-17-dev pkg-config
```

## Build

```bash
export SDK_3RDPARTY=/path/to/ossia-sdk-checkout/3rdparty   # from a recursive ossia/score checkout

cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DVST3_SDK_ROOT="$SDK_3RDPARTY/vst3" \
  -Dpybind11_DIR="$SDK_3RDPARTY/libossia/3rdparty/pybind11" \
  -DCMAKE_PREFIX_PATH="$SDK_3RDPARTY/libpd/pure-data/src"

cmake --build build
```

The compiled VST3 bundle is written under `build/vst3/`.

The exact, reproducible build steps for every platform live in
[`.github/workflows/build_cmake.yml`](.github/workflows/build_cmake.yml),
which also produces the rolling release.

## Known issue

VST3 UIs built from this template can crash *on UI open* inside
[ossia/score](https://github.com/ossia/score) on Linux while working fine in
Reaper — see [ossia/score#2023](https://github.com/ossia/score/issues/2023).
This is a host-side dynamic-link / Qt-runtime collision in score's VST3 UI
hosting, not a fault of the plugin itself (the same binaries load correctly in
other hosts).
