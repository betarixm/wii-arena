FROM ubuntu:24.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

ENV DOLPHIN_PREFIX=/opt/wii-arena-dolphin
ENV VULKAN_LAYER_PREFIX=/opt/wii-arena-vulkan-layer

ENV PATH=${DOLPHIN_PREFIX}/bin:${PATH}
ENV LD_LIBRARY_PATH=${DOLPHIN_PREFIX}/lib:${VULKAN_LAYER_PREFIX}/lib:${LD_LIBRARY_PATH}
ENV VK_LAYER_PATH=${VULKAN_LAYER_PREFIX}/share/vulkan/explicit_layer.d
ENV VK_INSTANCE_LAYERS=VK_LAYER_WII_arena_frame_capture
ENV DOLPHIN_DISPLAY=:99

RUN apt-get update && \
    apt-get install -y --no-install-recommends \
    locales wget xvfb xserver-xorg-core x11-utils x11vnc novnc websockify \
    git gcc g++ make python3-dev cmake \
    pkg-config libgl1-mesa-dev libx11-dev libxrandr-dev libxi-dev \
    libegl1-mesa-dev libvulkan-dev \
    libavcodec-dev libavformat-dev libavutil-dev libswresample-dev libswscale-dev \
    libudev-dev libevdev-dev \
    glslang-dev glslang-tools libpugixml-dev libxxhash-dev \
    libbz2-dev liblzma-dev libzstd-dev liblzo2-dev liblz4-dev libspng-dev \
    libusb-1.0-0-dev libminiupnpc-dev libcurl4-openssl-dev \
    libhidapi-dev libsystemd-dev libgtest-dev libasound2-dev \
    libpulse-dev llvm-dev libbluetooth-dev \
    qt6-base-dev qt6-base-private-dev qt6-svg-dev gettext && \
    wget https://github.com/VirtualGL/virtualgl/releases/download/3.1.4/virtualgl_3.1.4_amd64.deb && \
    apt-get install -y --no-install-recommends ./virtualgl_3.1.4_amd64.deb && \
    rm virtualgl_3.1.4_amd64.deb && \
    apt-get clean && rm -rf /var/lib/apt/lists/* && \
    locale-gen en_US.UTF-8

RUN --mount=type=bind,source=.,target=/workspace,rw=true \
    --mount=type=cache,target=/build \
    --mount=type=cache,target=/build-vulkan-layer \
    if [ -d /workspace/dolphin ]; then \
    DOLPHIN_SRC=/workspace/dolphin; \
    elif [ -d /workspace/containers/dolphin/dolphin ]; then \
    DOLPHIN_SRC=/workspace/containers/dolphin/dolphin; \
    else \
    echo "dolphin source directory not found"; exit 1; \
    fi && \
    cd "${DOLPHIN_SRC}" && \
    git -c submodule."Externals/Qt".update=none \
    -c submodule."Externals/FFmpeg-bin".update=none \
    -c submodule."Externals/libadrenotools".update=none \
    submodule update --init --recursive && \
    cmake -S . -B /build \
    -DCMAKE_INSTALL_PREFIX="${DOLPHIN_PREFIX}" \
    -DUSE_SYSTEM_SDL3=OFF \
    -DUSE_SYSTEM_FMT=OFF \
    -DUSE_SYSTEM_MINIZIP-NG=OFF \
    -DUSE_SYSTEM_SFML=OFF \
    -DUSE_SYSTEM_MBEDTLS=OFF \
    -DUSE_SYSTEM_LIBMGBA=OFF \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5 && \
    cmake --build /build -j"$(nproc)" && \
    cmake --install /build && \
    cmake -S /workspace/packages/vulkan-frame-layer -B /build-vulkan-layer \
    -DCMAKE_INSTALL_PREFIX="${VULKAN_LAYER_PREFIX}" && \
    cmake --build /build-vulkan-layer -j"$(nproc)" && \
    cmake --install /build-vulkan-layer

FROM builder AS runtime
COPY containers/dolphin/start-dolphin-gpu-x11.sh /opt/wii-arena/bin/start-dolphin-gpu-x11
RUN chmod +x /opt/wii-arena/bin/start-dolphin-gpu-x11
COPY configurations/dolphin/settings /dolphin-user

FROM scratch AS dolphin-artifact
COPY --from=builder /opt/wii-arena-dolphin/ /wii-arena-dolphin/

FROM scratch AS vulkan-layer-artifact
COPY --from=builder /opt/wii-arena-vulkan-layer/ /wii-arena-vulkan-layer/

FROM scratch AS full-artifact
COPY --from=builder /opt/wii-arena-dolphin/ /wii-arena-dolphin/
COPY --from=builder /opt/wii-arena-vulkan-layer/ /wii-arena-vulkan-layer/
