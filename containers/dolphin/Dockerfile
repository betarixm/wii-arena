FROM ubuntu:24.04 AS build-deps

ENV DEBIAN_FRONTEND=noninteractive

ENV DOLPHIN_PREFIX=/opt/wii-arena-dolphin
ENV VULKAN_LAYER_PREFIX=/opt/wii-arena-vulkan-layer

ENV PATH=${DOLPHIN_PREFIX}/bin:${PATH}
ENV LD_LIBRARY_PATH=${DOLPHIN_PREFIX}/lib:${VULKAN_LAYER_PREFIX}/lib

RUN apt-get update && \
    apt-get install -y --no-install-recommends \
    ca-certificates locales xserver-xorg-core x11-utils \
    git gcc g++ make python3-dev cmake \
    pkg-config libgl1-mesa-dev libx11-dev libxrandr-dev libxi-dev \
    libegl1-mesa-dev libvulkan-dev \
    libavcodec-dev libavformat-dev libavutil-dev libswresample-dev libswscale-dev \
    libudev-dev libevdev-dev \
    glslang-dev glslang-tools libpugixml-dev libxxhash-dev \
    libbz2-dev liblzma-dev libzstd-dev liblzo2-dev liblz4-dev libspng-dev \
    libusb-1.0-0-dev libcurl4-openssl-dev \
    libhidapi-dev libsystemd-dev libasound2-dev \
    libpulse-dev llvm-dev libbluetooth-dev && \
    apt-get clean && rm -rf /var/lib/apt/lists/* && \
    locale-gen en_US.UTF-8

FROM build-deps AS dolphin-builder

COPY .git/modules/containers/dolphin/dolphin/ /workspace/.git/modules/containers/dolphin/dolphin/
COPY containers/dolphin/dolphin/ /workspace/containers/dolphin/dolphin/

RUN --mount=type=cache,target=/build \
    cd /workspace/containers/dolphin/dolphin && \
    git submodule update --init --recursive -- \
    Externals/SDL/SDL \
    Externals/Vulkan-Headers \
    Externals/VulkanMemoryAllocator \
    Externals/cubeb/cubeb \
    Externals/enet/enet \
    Externals/fmt/fmt \
    Externals/implot/implot \
    Externals/minizip-ng/minizip-ng \
    Externals/spirv_cross/SPIRV-Cross \
    Externals/tinygltf/tinygltf \
    Externals/zlib-ng/zlib-ng && \
    cmake -S . -B /build \
    -DCMAKE_INSTALL_PREFIX="${DOLPHIN_PREFIX}" \
    -DENABLE_QT=OFF \
    -DENABLE_TESTS=OFF \
    -DENABLE_CLI_TOOL=OFF \
    -DENABLE_AUTOUPDATE=OFF \
    -DENABLE_ANALYTICS=OFF \
    -DUSE_DISCORD_PRESENCE=OFF \
    -DUSE_RETRO_ACHIEVEMENTS=OFF \
    -DUSE_UPNP=OFF \
    -DUSE_SYSTEM_SDL3=OFF \
    -DUSE_SYSTEM_FMT=OFF \
    -DUSE_SYSTEM_MINIZIP-NG=OFF \
    -DUSE_SYSTEM_SFML=OFF \
    -DUSE_SYSTEM_MBEDTLS=OFF \
    -DUSE_SYSTEM_LIBMGBA=OFF \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5 && \
    cmake --build /build -j"$(nproc)" && \
    cmake --install /build

FROM build-deps AS vulkan-layer-builder

COPY packages/vulkan-frame-layer/ /workspace/packages/vulkan-frame-layer/

RUN --mount=type=cache,target=/build-vulkan-layer \
    cmake -S /workspace/packages/vulkan-frame-layer -B /build-vulkan-layer \
    -DCMAKE_INSTALL_PREFIX="${VULKAN_LAYER_PREFIX}" && \
    cmake --build /build-vulkan-layer -j"$(nproc)" && \
    cmake --install /build-vulkan-layer

FROM build-deps AS runtime
ENV VK_LAYER_PATH=${VULKAN_LAYER_PREFIX}/share/vulkan/explicit_layer.d
ENV VK_INSTANCE_LAYERS=VK_LAYER_WII_arena_frame_capture
ENV FRAME_CAPTURE_EXPORT_RING_SIZE=3
ENV DOLPHIN_DISPLAY=:99

COPY --from=dolphin-builder /opt/wii-arena-dolphin/ /opt/wii-arena-dolphin/
COPY --from=vulkan-layer-builder /opt/wii-arena-vulkan-layer/ /opt/wii-arena-vulkan-layer/
COPY containers/dolphin/entrypoint.sh /opt/wii-arena/bin/entrypoint
COPY configurations/dolphin/scripts /scripts
RUN chmod +x /opt/wii-arena/bin/entrypoint
COPY configurations/dolphin/settings/ /dolphin-user/

FROM scratch AS dolphin-artifact
COPY --from=dolphin-builder /opt/wii-arena-dolphin/ /wii-arena-dolphin/

FROM scratch AS vulkan-layer-artifact
COPY --from=vulkan-layer-builder /opt/wii-arena-vulkan-layer/ /wii-arena-vulkan-layer/

FROM runtime AS default