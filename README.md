# TimeMachine

Using the Avendish processor template

## Set VST 3 SDK on Ubuntu 24.04 and install required dependencies

```bash
sudo apt-get update
sudo apt-get update
sudo apt-get install -y                 \
    build-essential                     \
    gcc-14                              \
    g++-14                              \
    cmake                               \
    libasound2-dev                      \
    libboost-dev                        \
    libcairo2-dev                       \
    libfontconfig1-dev                  \
    libgstreamer-plugins-base1.0-dev    \
    libgstreamer1.0-dev                 \
    libgtkmm-3.0-dev                    \
    libjack-jackd2-dev                  \
    libsoup2.4-dev                      \
    libsqlite3-dev                      \
    libwayland-dev                      \
    libx11-dev                          \
    libx11-xcb-dev                      \
    libxcb-cursor-dev                   \
    libxcb-keysyms1-dev                 \
    libxcb-util-dev                     \
    libxcb-xkb-dev                      \
    libxkbcommon-dev                    \
    libxkbcommon-x11-dev                \
    ninja-build                         \
    pkg-config                          \
    pybind11-dev                        \
    python3-dev                         \
    wayland-protocols
```

sudo apt update
sudo apt install -y 

Clone the VST 3 SDK

```bash
git clone --recursive https://github.com/steinbergmedia/vst3sdk.git
cd vst3sdk
```

## Configure 

Important notes:

- replace $HOME with your vst3sdk folder location.
- The Wayland / ECM Bug: If you have Qt6 development libraries installed (e.g., for ossia score), a package called extra-cmake-modules (ECM) will intercept the VST3 SDK's search for Wayland and crash the build. To fix this, we temporarily hide the ECM Wayland file, configure the project, and then immediately restore the file.

```bash
sudo mv /usr/lib/x86_64-linux-gnu/cmake/Qt6/3rdparty/extra-cmake-modules/find-modules/FindWayland.cmake /usr/lib/x86_64-linux-gnu/cmake/Qt6/3rdparty/extra-cmake-modules/find-modules/FindWayland.cmake.bak
mkdir -p build
cd build
cmake -G Ninja .. \
  -DCMAKE_C_COMPILER=gcc-14 \
  -DCMAKE_CXX_COMPILER=g++-14 \
  -DCMAKE_BUILD_TYPE=Release \
  -DVST3_SDK_ROOT=$HOME/vst3sdk
sudo mv /usr/lib/x86_64-linux-gnu/cmake/Qt6/3rdparty/extra-cmake-modules/find-modules/FindWayland.cmake.bak /usr/lib/x86_64-linux-gnu/cmake/Qt6/3rdparty/extra-cmake-modules/find-modules/FindWayland.cmake
```

## Build

Go to the `build` folder and run either `ninja` or `cmake --build .`

Your compiled .vst3 bundle will be automatically symlinked/copied to `~/.vst3` and ready to use in your host applications.