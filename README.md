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

## Build

`CMakeLists.txt` follows the upstream template (`avnd_addon_init` /
`avnd_addon_object` / `avnd_addon_finalize`) and fetches **Avendish** and
**pffft** automatically. You provide a C++20 compiler, the **Steinberg VST3
SDK**, and **Boost** headers:

```bash
git clone --recursive https://github.com/steinbergmedia/vst3sdk

cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DVST3_SDK_ROOT="$PWD/vst3sdk" \
  -DBOOST_ROOT=/path/to/boost \
  -DSMTG_ENABLE_VSTGUI_SUPPORT=OFF -DSMTG_ADD_VSTGUI=OFF \
  -DSMTG_CREATE_PLUGIN_LINK=OFF

cmake --build build --target avnd_time_machine_vst3
```

The compiled VST3 bundle is written under `build/vst3/avnd_time_machine.vst3`.

The exact, reproducible build for every platform lives in
[`.github/workflows/build_cmake.yml`](.github/workflows/build_cmake.yml),
which mirrors ossia's `avnd-addon` recipe and produces the rolling release.
(The upstream template drives CI from ossia's reusable workflow; that
workflow's access is org-restricted, so this fork ships an equivalent
self-contained build instead.)

## Known issue

VST3 UIs built from this template can crash *on UI open* inside
[ossia/score](https://github.com/ossia/score) on Linux while working fine in
Reaper — see [ossia/score#2023](https://github.com/ossia/score/issues/2023).
This is a host-side dynamic-link / Qt-runtime collision in score's VST3 UI
hosting, not a fault of the plugin itself (the same binaries load correctly in
other hosts).
