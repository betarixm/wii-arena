FROM ubuntu:24.04@sha256:c4a8d5503dfb2a3eb8ab5f807da5bc69a85730fb49b5cfca2330194ebcc41c7b

ENV DEBIAN_FRONTEND=noninteractive
ENV VK_LAYER_PATH=/usr/local/share/vulkan/explicit_layer.d
ENV VK_INSTANCE_LAYERS=VK_LAYER_WII_arena_frame_capture
RUN apt-get update && \
    apt-get install -y --no-install-recommends \
    locales wget xvfb x11vnc novnc websockify git gcc g++ make python3-dev cmake \
    # PkgConfig
    pkg-config \
    # OpenGL
    libgl1-mesa-dev \
    # X11
    libx11-dev \
    libxrandr-dev \
    libxi-dev \
    # EGL
    libegl1-mesa-dev \
    # Vulkan
    libvulkan-dev \
    # FFMPEG
    libavcodec-dev \
    libavformat-dev \
    libavutil-dev \
    libswresample-dev \
    libswscale-dev \
    # udev (Use libeudev-dev if on non-systemd)
    libudev-dev \
    # evdev
    libevdev-dev \
    # SDL
    # libsdl3-dev \
    # FMT
    # libfmt-dev \
    # glslang
    glslang-dev \
    glslang-tools \
    # pugixml
    libpugixml-dev \
    # enet
    # libenet-dev \
    # xxhash
    libxxhash-dev \
    # bzip2
    libbz2-dev \
    # LZMA
    liblzma-dev \
    # zstd
    libzstd-dev \
    # zlib
    # zlib1g-dev \
    # minizip-ng
    ## Not packaged yet (but soon on Debian)
    # lzo
    liblzo2-dev \
    # lz4
    liblz4-dev \
    # spng
    libspng-dev \
    # cubeb
    libcubeb-dev \
    # libusb
    libusb-1.0-0-dev \
    # SFML
    ## libsfml-dev \
    ## SFML 3.0 is not yet shipped on Debian and Ubuntu
    # MiniUPNPC
    libminiupnpc-dev \
    # MbedTLS
    ## We are using an outdated 2.x version, and Debian/Ubuntu only ship 3.x now
    # cURL (this could also be libcurl4-gnutls-dev)
    libcurl4-openssl-dev \
    # hidapi
    libhidapi-dev \
    # mgba
    ## libmgba-dev \
    ## Newer MGBA versions are currently broken with Dolphin
    # systemd
    libsystemd-dev \
    # gtest
    libgtest-dev \
    # ALSA
    libasound2-dev \
    # PulseAudio
    libpulse-dev \
    # LLVM
    llvm-dev \
    # BlueZ
    libbluetooth-dev \
    # Qt6
    qt6-base-dev \
    qt6-base-private-dev \
    qt6-svg-dev \
    # Gettext
    gettext \
    && wget https://github.com/VirtualGL/virtualgl/releases/download/3.1.4/virtualgl_3.1.4_amd64.deb && \
    apt-get install -y --no-install-recommends ./virtualgl_3.1.4_amd64.deb \
    && rm virtualgl_3.1.4_amd64.deb \
    && apt-get clean \
    && rm -rf /var/lib/apt/lists/* \
    && locale-gen en_US.UTF-8

RUN --mount=type=bind,source=.,target=/workspace,rw=true \
    --mount=type=cache,target=/build \
    --mount=type=cache,target=/build-vulkan-layer \
    if [ -d /workspace/dolphin ]; then SRC=/workspace/dolphin; \
    elif [ -d /workspace/containers/dolphin/dolphin ]; then SRC=/workspace/containers/dolphin/dolphin; \
    else echo "dolphin source directory not found in build context"; exit 1; fi && \
    cd "$SRC" && \
    git -c submodule."Externals/Qt".update=none \
    -c submodule."Externals/FFmpeg-bin".update=none \
    -c submodule."Externals/libadrenotools".update=none \
    submodule update --init --recursive && \
    cmake -S . -B /build \
    -DUSE_SYSTEM_SDL3=OFF \
    -DUSE_SYSTEM_FMT=OFF \
    -DUSE_SYSTEM_MINIZIP-NG=OFF \
    -DUSE_SYSTEM_SFML=OFF \
    -DUSE_SYSTEM_MBEDTLS=OFF \
    -DUSE_SYSTEM_LIBMGBA=OFF \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5 && \
    cmake --build /build -j$(nproc) && \
    cmake --install /build && \
    cmake -S /workspace/packages/vulkan-frame-layer -B /build-vulkan-layer && \
    cmake --build /build-vulkan-layer -j$(nproc) && \
    cmake --install /build-vulkan-layer
