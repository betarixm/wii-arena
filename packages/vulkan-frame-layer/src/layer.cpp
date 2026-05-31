#include <vulkan/vk_layer.h>
#include <vulkan/vulkan.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>
#include <unordered_map>
#include <vector>

extern "C" VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance instance,
                                                                          const char *name);
extern "C" VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice device,
                                                                        const char *name);

struct ExportedResource {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
};

struct CapturedFrame {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t stride = 0;
    uint32_t size = 0;
    uint32_t frame_id = 0;
    VkFormat format = VK_FORMAT_UNDEFINED;
};

struct SwapchainInfo {
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkExtent2D extent{};
    std::vector<VkImage> images;
};

struct QueueInfo {
    uint32_t family = 0;
    VkQueueFlags flags = 0;
};

static PFN_vkGetInstanceProcAddr g_next_gipa = nullptr;
static PFN_vkGetDeviceProcAddr g_next_gdpa = nullptr;
static PFN_vkGetPhysicalDeviceMemoryProperties g_get_memory_props = nullptr;
static PFN_vkGetPhysicalDeviceQueueFamilyProperties g_get_queue_family_props = nullptr;
static PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR g_get_surface_capabilities = nullptr;

static PFN_vkCreateDevice g_next_create_device = nullptr;
static PFN_vkCreateSwapchainKHR g_next_create_swapchain_khr = nullptr;
static PFN_vkDestroySwapchainKHR g_next_destroy_swapchain_khr = nullptr;
static PFN_vkGetSwapchainImagesKHR g_next_get_swapchain_images_khr = nullptr;
static PFN_vkQueuePresentKHR g_next_queue_present_khr = nullptr;
static PFN_vkGetDeviceQueue g_next_get_device_queue = nullptr;
static PFN_vkQueueSubmit g_next_queue_submit = nullptr;
static PFN_vkQueueWaitIdle g_next_queue_wait_idle = nullptr;
static PFN_vkCreateBuffer g_next_create_buffer = nullptr;
static PFN_vkDestroyBuffer g_next_destroy_buffer = nullptr;
static PFN_vkGetBufferMemoryRequirements g_next_get_buffer_memory_requirements = nullptr;
static PFN_vkAllocateMemory g_next_allocate_memory = nullptr;
static PFN_vkFreeMemory g_next_free_memory = nullptr;
static PFN_vkBindBufferMemory g_next_bind_buffer_memory = nullptr;
static PFN_vkGetMemoryFdKHR g_next_get_memory_fd_khr = nullptr;
static PFN_vkCreateCommandPool g_next_create_command_pool = nullptr;
static PFN_vkDestroyCommandPool g_next_destroy_command_pool = nullptr;
static PFN_vkAllocateCommandBuffers g_next_allocate_command_buffers = nullptr;
static PFN_vkFreeCommandBuffers g_next_free_command_buffers = nullptr;
static PFN_vkBeginCommandBuffer g_next_begin_command_buffer = nullptr;
static PFN_vkEndCommandBuffer g_next_end_command_buffer = nullptr;
static PFN_vkCmdPipelineBarrier g_next_cmd_pipeline_barrier = nullptr;
static PFN_vkCmdCopyImageToBuffer g_next_cmd_copy_image_to_buffer = nullptr;

static VkPhysicalDevice g_physical_device = VK_NULL_HANDLE;
static VkDevice g_device = VK_NULL_HANDLE;
static VkPhysicalDeviceMemoryProperties g_memory_props{};
static uint32_t g_default_queue_family = 0;
static std::vector<VkQueueFlags> g_queue_family_flags;

static std::atomic<bool> g_server_started{false};
static std::atomic<uint32_t> g_frame_id{0};
static std::atomic<uint32_t> g_capture_fail_logs{0};
static std::mutex g_mu;
static std::mutex g_export_mu;
static std::unordered_map<VkSwapchainKHR, SwapchainInfo> g_swapchains;
static std::unordered_map<VkQueue, QueueInfo> g_queues;
static std::deque<ExportedResource> g_retained_exports;
static CapturedFrame g_latest_frame;

static bool verbose_logging() {
    const char *raw = std::getenv("FRAME_CAPTURE_VERBOSE");
    return raw && std::strcmp(raw, "0") != 0 && raw[0] != '\0';
}

static uint32_t env_u32(const char *name, uint32_t fallback) {
    const char *raw = std::getenv(name);
    if (!raw || !*raw) {
        return fallback;
    }
    char *end = nullptr;
    unsigned long value = std::strtoul(raw, &end, 10);
    return end == raw ? fallback : static_cast<uint32_t>(value);
}

static bool is_supported_format(VkFormat format) {
    return format == VK_FORMAT_R8G8B8A8_UNORM || format == VK_FORMAT_R8G8B8A8_SRGB ||
           format == VK_FORMAT_B8G8R8A8_UNORM || format == VK_FORMAT_B8G8R8A8_SRGB;
}

static bool queue_family_supports_transfer(uint32_t family) {
    return family < g_queue_family_flags.size() &&
           (g_queue_family_flags[family] & VK_QUEUE_TRANSFER_BIT);
}

static uint32_t find_memory_type(uint32_t bits, VkMemoryPropertyFlags flags) {
    for (uint32_t i = 0; i < g_memory_props.memoryTypeCount; ++i) {
        if ((bits & (1u << i)) && (g_memory_props.memoryTypes[i].propertyFlags & flags) == flags) {
            return i;
        }
    }
    return UINT32_MAX;
}

static const char *socket_path() {
    const char *path = std::getenv("FRAME_CAPTURE_SOCKET");
    if (!path || !*path) {
        path = "/tmp/frame_capture.sock";
    }
    return path;
}

static void log_capture_failure(const char *label, int code = 0) {
    if (g_capture_fail_logs.fetch_add(1) < 80) {
        std::fprintf(stderr, "[frame-capture] %s code=%d\n", label, code);
    }
}

static void destroy_exported_resource(const ExportedResource &resource) {
    if (resource.buffer != VK_NULL_HANDLE && g_next_destroy_buffer) {
        g_next_destroy_buffer(g_device, resource.buffer, nullptr);
    }
    if (resource.memory != VK_NULL_HANDLE && g_next_free_memory) {
        g_next_free_memory(g_device, resource.memory, nullptr);
    }
}

static void destroy_buffer_memory(VkBuffer buffer, VkDeviceMemory memory) {
    if (buffer != VK_NULL_HANDLE) {
        g_next_destroy_buffer(g_device, buffer, nullptr);
    }
    if (memory != VK_NULL_HANDLE) {
        g_next_free_memory(g_device, memory, nullptr);
    }
}

static void retain_exported_resource_locked(VkBuffer buffer, VkDeviceMemory memory) {
    if (buffer == VK_NULL_HANDLE && memory == VK_NULL_HANDLE) {
        return;
    }
    const uint32_t retain_count = env_u32("FRAME_CAPTURE_RETAINED_EXPORTS", 16);
    g_retained_exports.push_back(ExportedResource{buffer, memory});
    while (retain_count > 0 && g_retained_exports.size() > retain_count) {
        destroy_exported_resource(g_retained_exports.front());
        g_retained_exports.pop_front();
    }
}

static bool send_latest_frame_fd(int client) {
    std::lock_guard<std::mutex> lock(g_export_mu);
    if (g_latest_frame.memory == VK_NULL_HANDLE || !g_next_get_memory_fd_khr) {
        const char no_frame = 'N';
        return write(client, &no_frame, 1) == 1;
    }

    VkMemoryGetFdInfoKHR fd_info{};
    fd_info.sType = VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR;
    fd_info.memory = g_latest_frame.memory;
    fd_info.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
    int fd = -1;
    VkResult rc = g_next_get_memory_fd_khr(g_device, &fd_info, &fd);
    if (rc != VK_SUCCESS || fd < 0) {
        log_capture_failure("export latest fd failed", rc);
        const char no_frame = 'N';
        return write(client, &no_frame, 1) == 1;
    }

    uint32_t header[6] = {
        g_latest_frame.width,
        g_latest_frame.height,
        g_latest_frame.stride,
        g_latest_frame.size,
        g_latest_frame.frame_id,
        static_cast<uint32_t>(g_latest_frame.format),
    };
    iovec iov{};
    iov.iov_base = header;
    iov.iov_len = sizeof(header);

    char control[CMSG_SPACE(sizeof(int))];
    std::memset(control, 0, sizeof(control));
    msghdr msg{};
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = control;
    msg.msg_controllen = sizeof(control);

    cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(sizeof(int));
    std::memcpy(CMSG_DATA(cmsg), &fd, sizeof(int));
    msg.msg_controllen = cmsg->cmsg_len;

    const bool sent = sendmsg(client, &msg, MSG_NOSIGNAL) >= 0;
    close(fd);
    return sent;
}

static void request_server_loop() {
    int server = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (server < 0) {
        std::fprintf(stderr, "[frame-capture] socket server create failed\n");
        return;
    }

    sockaddr_un addr{};
    const char *path = socket_path();
    if (std::strlen(path) >= sizeof(addr.sun_path)) {
        std::fprintf(stderr, "[frame-capture] socket path too long: %s\n", path);
        close(server);
        return;
    }
    unlink(path);
    addr.sun_family = AF_UNIX;
    std::snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path);
    if (bind(server, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0 ||
        listen(server, 1) != 0) {
        std::fprintf(stderr, "[frame-capture] socket server bind/listen failed: %s\n", path);
        close(server);
        return;
    }
    chmod(path, 0666);

    std::fprintf(stderr, "[frame-capture] request socket listening: %s\n", path);
    while (true) {
        int client = accept4(server, nullptr, nullptr, SOCK_CLOEXEC);
        if (client < 0) {
            continue;
        }
        char command = 0;
        while (read(client, &command, 1) == 1) {
            if (command == 'R') {
                if (!send_latest_frame_fd(client)) {
                    break;
                }
            } else if (command == 'Q') {
                break;
            }
        }
        close(client);
    }
}

static void start_request_server_once() {
    bool expected = false;
    if (!g_server_started.compare_exchange_strong(expected, true)) {
        return;
    }
    std::thread(request_server_loop).detach();
}

static void load_device_functions(VkDevice device) {
    g_next_create_swapchain_khr = reinterpret_cast<PFN_vkCreateSwapchainKHR>(
        g_next_gdpa(device, "vkCreateSwapchainKHR"));
    g_next_destroy_swapchain_khr = reinterpret_cast<PFN_vkDestroySwapchainKHR>(
        g_next_gdpa(device, "vkDestroySwapchainKHR"));
    g_next_get_swapchain_images_khr = reinterpret_cast<PFN_vkGetSwapchainImagesKHR>(
        g_next_gdpa(device, "vkGetSwapchainImagesKHR"));
    g_next_queue_present_khr =
        reinterpret_cast<PFN_vkQueuePresentKHR>(g_next_gdpa(device, "vkQueuePresentKHR"));
    g_next_get_device_queue =
        reinterpret_cast<PFN_vkGetDeviceQueue>(g_next_gdpa(device, "vkGetDeviceQueue"));
    g_next_queue_submit = reinterpret_cast<PFN_vkQueueSubmit>(g_next_gdpa(device, "vkQueueSubmit"));
    g_next_queue_wait_idle =
        reinterpret_cast<PFN_vkQueueWaitIdle>(g_next_gdpa(device, "vkQueueWaitIdle"));
    g_next_create_buffer =
        reinterpret_cast<PFN_vkCreateBuffer>(g_next_gdpa(device, "vkCreateBuffer"));
    g_next_destroy_buffer =
        reinterpret_cast<PFN_vkDestroyBuffer>(g_next_gdpa(device, "vkDestroyBuffer"));
    g_next_get_buffer_memory_requirements = reinterpret_cast<PFN_vkGetBufferMemoryRequirements>(
        g_next_gdpa(device, "vkGetBufferMemoryRequirements"));
    g_next_allocate_memory =
        reinterpret_cast<PFN_vkAllocateMemory>(g_next_gdpa(device, "vkAllocateMemory"));
    g_next_free_memory = reinterpret_cast<PFN_vkFreeMemory>(g_next_gdpa(device, "vkFreeMemory"));
    g_next_bind_buffer_memory =
        reinterpret_cast<PFN_vkBindBufferMemory>(g_next_gdpa(device, "vkBindBufferMemory"));
    g_next_get_memory_fd_khr =
        reinterpret_cast<PFN_vkGetMemoryFdKHR>(g_next_gdpa(device, "vkGetMemoryFdKHR"));
    g_next_create_command_pool =
        reinterpret_cast<PFN_vkCreateCommandPool>(g_next_gdpa(device, "vkCreateCommandPool"));
    g_next_destroy_command_pool =
        reinterpret_cast<PFN_vkDestroyCommandPool>(g_next_gdpa(device, "vkDestroyCommandPool"));
    g_next_allocate_command_buffers = reinterpret_cast<PFN_vkAllocateCommandBuffers>(
        g_next_gdpa(device, "vkAllocateCommandBuffers"));
    g_next_free_command_buffers =
        reinterpret_cast<PFN_vkFreeCommandBuffers>(g_next_gdpa(device, "vkFreeCommandBuffers"));
    g_next_begin_command_buffer =
        reinterpret_cast<PFN_vkBeginCommandBuffer>(g_next_gdpa(device, "vkBeginCommandBuffer"));
    g_next_end_command_buffer =
        reinterpret_cast<PFN_vkEndCommandBuffer>(g_next_gdpa(device, "vkEndCommandBuffer"));
    g_next_cmd_pipeline_barrier =
        reinterpret_cast<PFN_vkCmdPipelineBarrier>(g_next_gdpa(device, "vkCmdPipelineBarrier"));
    g_next_cmd_copy_image_to_buffer =
        reinterpret_cast<PFN_vkCmdCopyImageToBuffer>(g_next_gdpa(device, "vkCmdCopyImageToBuffer"));
}

static bool choose_capture_queue(VkQueue present_queue, VkQueue *capture_queue,
                                 uint32_t *capture_family) {
    {
        std::lock_guard<std::mutex> lock(g_mu);
        auto present_it = g_queues.find(present_queue);
        if (present_it != g_queues.end() && (present_it->second.flags & VK_QUEUE_TRANSFER_BIT)) {
            *capture_queue = present_queue;
            *capture_family = present_it->second.family;
            return true;
        }
        for (const auto &entry : g_queues) {
            if (entry.second.flags & VK_QUEUE_TRANSFER_BIT) {
                *capture_queue = entry.first;
                *capture_family = entry.second.family;
                return true;
            }
        }
    }
    if (queue_family_supports_transfer(g_default_queue_family)) {
        *capture_queue = present_queue;
        *capture_family = g_default_queue_family;
        return true;
    }
    return false;
}

static bool create_export_buffer(VkDeviceSize payload_size, VkBuffer *buffer, VkDeviceMemory *memory,
                                 VkDeviceSize *allocation_size) {
    VkExternalMemoryBufferCreateInfo ext_buf{};
    ext_buf.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO;
    ext_buf.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;

    VkBufferCreateInfo buffer_ci{};
    buffer_ci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_ci.pNext = &ext_buf;
    buffer_ci.size = payload_size;
    buffer_ci.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    buffer_ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkResult rc = g_next_create_buffer(g_device, &buffer_ci, nullptr, buffer);
    if (rc != VK_SUCCESS) {
        log_capture_failure("vkCreateBuffer failed", rc);
        return false;
    }

    VkMemoryRequirements req{};
    g_next_get_buffer_memory_requirements(g_device, *buffer, &req);
    uint32_t mem_type = find_memory_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (mem_type == UINT32_MAX) {
        for (uint32_t i = 0; i < g_memory_props.memoryTypeCount; ++i) {
            if (req.memoryTypeBits & (1u << i)) {
                mem_type = i;
                break;
            }
        }
    }
    if (mem_type == UINT32_MAX) {
        log_capture_failure("no compatible export memory type");
        g_next_destroy_buffer(g_device, *buffer, nullptr);
        *buffer = VK_NULL_HANDLE;
        return false;
    }

    VkExportMemoryAllocateInfo export_ai{};
    export_ai.sType = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO;
    export_ai.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;

    VkMemoryAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc.pNext = &export_ai;
    alloc.allocationSize = req.size;
    alloc.memoryTypeIndex = mem_type;

    rc = g_next_allocate_memory(g_device, &alloc, nullptr, memory);
    if (rc != VK_SUCCESS) {
        log_capture_failure("vkAllocateMemory failed", rc);
        g_next_destroy_buffer(g_device, *buffer, nullptr);
        *buffer = VK_NULL_HANDLE;
        return false;
    }
    rc = g_next_bind_buffer_memory(g_device, *buffer, *memory, 0);
    if (rc != VK_SUCCESS) {
        log_capture_failure("vkBindBufferMemory failed", rc);
        destroy_buffer_memory(*buffer, *memory);
        *buffer = VK_NULL_HANDLE;
        *memory = VK_NULL_HANDLE;
        return false;
    }

    *allocation_size = req.size;
    return true;
}

static bool capture_present_image(VkQueue present_queue, VkSwapchainKHR swapchain,
                                  uint32_t image_index, const VkSemaphore *wait_semaphores,
                                  uint32_t wait_semaphore_count) {
    if (!g_device || !g_next_queue_submit || !g_next_queue_wait_idle ||
        !g_next_cmd_copy_image_to_buffer || !g_next_cmd_pipeline_barrier) {
        log_capture_failure("capture unavailable");
        return false;
    }

    SwapchainInfo swapchain_info;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        auto it = g_swapchains.find(swapchain);
        if (it == g_swapchains.end()) {
            log_capture_failure("unknown swapchain");
            return false;
        }
        swapchain_info = it->second;
    }
    if (image_index >= swapchain_info.images.size()) {
        log_capture_failure("present image index out of range");
        return false;
    }
    if (!is_supported_format(swapchain_info.format)) {
        log_capture_failure("unsupported swapchain format",
                            static_cast<int>(swapchain_info.format));
        return false;
    }

    VkQueue capture_queue = VK_NULL_HANDLE;
    uint32_t capture_family = 0;
    if (!choose_capture_queue(present_queue, &capture_queue, &capture_family)) {
        log_capture_failure("no transfer-capable queue for present capture");
        return false;
    }

    const uint32_t width = swapchain_info.extent.width;
    const uint32_t height = swapchain_info.extent.height;
    const uint32_t stride = width * 4;
    const VkDeviceSize payload_size = static_cast<VkDeviceSize>(stride) * height;

    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceSize allocation_size = 0;
    if (!create_export_buffer(payload_size, &buffer, &memory, &allocation_size)) {
        return false;
    }

    VkCommandPoolCreateInfo pool_ci{};
    pool_ci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_ci.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    pool_ci.queueFamilyIndex = capture_family;
    VkCommandPool pool = VK_NULL_HANDLE;
    VkResult rc = g_next_create_command_pool(g_device, &pool_ci, nullptr, &pool);
    if (rc != VK_SUCCESS) {
        log_capture_failure("vkCreateCommandPool failed", rc);
        destroy_buffer_memory(buffer, memory);
        return false;
    }

    VkCommandBufferAllocateInfo cmd_ai{};
    cmd_ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmd_ai.commandPool = pool;
    cmd_ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmd_ai.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    rc = g_next_allocate_command_buffers(g_device, &cmd_ai, &cmd);
    if (rc != VK_SUCCESS) {
        log_capture_failure("vkAllocateCommandBuffers failed", rc);
        g_next_destroy_command_pool(g_device, pool, nullptr);
        destroy_buffer_memory(buffer, memory);
        return false;
    }

    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    rc = g_next_begin_command_buffer(cmd, &begin);
    if (rc != VK_SUCCESS) {
        log_capture_failure("vkBeginCommandBuffer failed", rc);
        g_next_free_command_buffers(g_device, pool, 1, &cmd);
        g_next_destroy_command_pool(g_device, pool, nullptr);
        destroy_buffer_memory(buffer, memory);
        return false;
    }

    VkImage image = swapchain_info.images[image_index];
    VkImageMemoryBarrier to_transfer{};
    to_transfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    to_transfer.srcAccessMask = 0;
    to_transfer.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    to_transfer.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    to_transfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    to_transfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_transfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_transfer.image = image;
    to_transfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    to_transfer.subresourceRange.levelCount = 1;
    to_transfer.subresourceRange.layerCount = 1;
    g_next_cmd_pipeline_barrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                                &to_transfer);

    VkBufferImageCopy region{};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = {width, height, 1};
    g_next_cmd_copy_image_to_buffer(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer, 1,
                                    &region);

    VkImageMemoryBarrier back = to_transfer;
    back.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    back.dstAccessMask = 0;
    back.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    back.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    g_next_cmd_pipeline_barrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1,
                                &back);

    rc = g_next_end_command_buffer(cmd);
    if (rc != VK_SUCCESS) {
        log_capture_failure("vkEndCommandBuffer failed", rc);
        g_next_free_command_buffers(g_device, pool, 1, &cmd);
        g_next_destroy_command_pool(g_device, pool, nullptr);
        destroy_buffer_memory(buffer, memory);
        return false;
    }

    std::vector<VkPipelineStageFlags> wait_stages(wait_semaphore_count,
                                                  VK_PIPELINE_STAGE_TRANSFER_BIT);
    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.waitSemaphoreCount = wait_semaphore_count;
    submit.pWaitSemaphores = wait_semaphores;
    submit.pWaitDstStageMask = wait_stages.empty() ? nullptr : wait_stages.data();
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;

    rc = g_next_queue_submit(capture_queue, 1, &submit, VK_NULL_HANDLE);
    if (rc != VK_SUCCESS) {
        log_capture_failure("vkQueueSubmit(capture) failed", rc);
        g_next_free_command_buffers(g_device, pool, 1, &cmd);
        g_next_destroy_command_pool(g_device, pool, nullptr);
        destroy_buffer_memory(buffer, memory);
        return false;
    }
    rc = g_next_queue_wait_idle(capture_queue);
    if (rc != VK_SUCCESS) {
        log_capture_failure("vkQueueWaitIdle(capture) failed", rc);
        g_next_free_command_buffers(g_device, pool, 1, &cmd);
        g_next_destroy_command_pool(g_device, pool, nullptr);
        destroy_buffer_memory(buffer, memory);
        return false;
    }

    g_next_free_command_buffers(g_device, pool, 1, &cmd);
    g_next_destroy_command_pool(g_device, pool, nullptr);
    {
        std::lock_guard<std::mutex> lock(g_export_mu);
        retain_exported_resource_locked(g_latest_frame.buffer, g_latest_frame.memory);
        g_latest_frame = CapturedFrame{
            buffer,
            memory,
            width,
            height,
            stride,
            static_cast<uint32_t>(allocation_size),
            g_frame_id.fetch_add(1),
            swapchain_info.format,
        };
    }
    if (verbose_logging()) {
        std::fprintf(stderr, "[frame-capture] captured present frame %ux%u fmt=%d size=%u\n",
                     width, height, swapchain_info.format, static_cast<uint32_t>(allocation_size));
    }
    return true;
}

extern "C" VKAPI_ATTR VkResult VKAPI_CALL
vkNegotiateLoaderLayerInterfaceVersion(VkNegotiateLayerInterface *version) {
    if (!version) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    version->loaderLayerInterfaceVersion = 2;
    version->pfnGetInstanceProcAddr = vkGetInstanceProcAddr;
    version->pfnGetDeviceProcAddr = vkGetDeviceProcAddr;
    return VK_SUCCESS;
}

extern "C" VKAPI_ATTR VkResult VKAPI_CALL vkCreateInstance(const VkInstanceCreateInfo *create_info,
                                                           const VkAllocationCallbacks *allocator,
                                                           VkInstance *instance) {
    auto *chain = const_cast<VkLayerInstanceCreateInfo *>(
        reinterpret_cast<const VkLayerInstanceCreateInfo *>(create_info->pNext));
    while (chain && !(chain->sType == VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO &&
                      chain->function == VK_LAYER_LINK_INFO)) {
        chain = const_cast<VkLayerInstanceCreateInfo *>(
            reinterpret_cast<const VkLayerInstanceCreateInfo *>(chain->pNext));
    }
    if (!chain) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    g_next_gipa = chain->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    auto next_create_instance =
        reinterpret_cast<PFN_vkCreateInstance>(g_next_gipa(nullptr, "vkCreateInstance"));
    chain->u.pLayerInfo = chain->u.pLayerInfo->pNext;
    VkResult result = next_create_instance(create_info, allocator, instance);
    if (result == VK_SUCCESS) {
        g_get_memory_props = reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties>(
            g_next_gipa(*instance, "vkGetPhysicalDeviceMemoryProperties"));
        g_get_queue_family_props = reinterpret_cast<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(
            g_next_gipa(*instance, "vkGetPhysicalDeviceQueueFamilyProperties"));
        g_get_surface_capabilities =
            reinterpret_cast<PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR>(
                g_next_gipa(*instance, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR"));
    }
    return result;
}

extern "C" VKAPI_ATTR VkResult VKAPI_CALL vkCreateDevice(VkPhysicalDevice physical_device,
                                                         const VkDeviceCreateInfo *create_info,
                                                         const VkAllocationCallbacks *allocator,
                                                         VkDevice *device) {
    auto *chain = const_cast<VkLayerDeviceCreateInfo *>(
        reinterpret_cast<const VkLayerDeviceCreateInfo *>(create_info->pNext));
    while (chain && !(chain->sType == VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO &&
                      chain->function == VK_LAYER_LINK_INFO)) {
        chain = const_cast<VkLayerDeviceCreateInfo *>(
            reinterpret_cast<const VkLayerDeviceCreateInfo *>(chain->pNext));
    }
    if (!chain || !g_next_gipa) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    g_next_gdpa = chain->u.pLayerInfo->pfnNextGetDeviceProcAddr;
    g_next_create_device =
        reinterpret_cast<PFN_vkCreateDevice>(g_next_gipa(nullptr, "vkCreateDevice"));
    chain->u.pLayerInfo = chain->u.pLayerInfo->pNext;

    std::vector<const char *> extensions;
    for (uint32_t i = 0; i < create_info->enabledExtensionCount; ++i) {
        extensions.push_back(create_info->ppEnabledExtensionNames[i]);
    }
    auto add_extension = [&](const char *name) {
        for (const char *existing : extensions) {
            if (std::strcmp(existing, name) == 0) {
                return;
            }
        }
        extensions.push_back(name);
    };
    add_extension(VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME);
    add_extension(VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME);

    VkDeviceCreateInfo patched_create_info = *create_info;
    patched_create_info.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    patched_create_info.ppEnabledExtensionNames = extensions.data();

    VkResult result =
        g_next_create_device(physical_device, &patched_create_info, allocator, device);
    if (result == VK_SUCCESS) {
        g_physical_device = physical_device;
        g_device = *device;
        if (create_info->queueCreateInfoCount > 0) {
            g_default_queue_family = create_info->pQueueCreateInfos[0].queueFamilyIndex;
        }
        if (g_get_memory_props) {
            g_get_memory_props(physical_device, &g_memory_props);
        }
        if (g_get_queue_family_props) {
            uint32_t count = 0;
            g_get_queue_family_props(physical_device, &count, nullptr);
            std::vector<VkQueueFamilyProperties> props(count);
            g_get_queue_family_props(physical_device, &count, props.data());
            g_queue_family_flags.clear();
            for (const auto &prop : props) {
                g_queue_family_flags.push_back(prop.queueFlags);
            }
        }
        load_device_functions(*device);
        start_request_server_once();
        if (verbose_logging()) {
            std::fprintf(stderr,
                         "[frame-capture] vkCreateDevice ok queue_family=%u memoryTypes=%u\n",
                         g_default_queue_family, g_memory_props.memoryTypeCount);
        }
    }
    return result;
}

extern "C" VKAPI_ATTR VkResult VKAPI_CALL
vkCreateSwapchainKHR(VkDevice device, const VkSwapchainCreateInfoKHR *create_info,
                     const VkAllocationCallbacks *allocator, VkSwapchainKHR *swapchain) {
    if (!g_next_create_swapchain_khr || !create_info) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkSwapchainCreateInfoKHR patched_create_info = *create_info;
    if (!(patched_create_info.imageUsage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT)) {
        if (g_get_surface_capabilities && g_physical_device != VK_NULL_HANDLE) {
            VkSurfaceCapabilitiesKHR caps{};
            VkResult caps_result =
                g_get_surface_capabilities(g_physical_device, create_info->surface, &caps);
            if (caps_result == VK_SUCCESS &&
                !(caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT)) {
                std::fprintf(stderr,
                             "[frame-capture] swapchain surface does not support TRANSFER_SRC\n");
                return VK_ERROR_FEATURE_NOT_PRESENT;
            }
        }
        patched_create_info.imageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    }

    VkResult result =
        g_next_create_swapchain_khr(device, &patched_create_info, allocator, swapchain);
    if (result == VK_SUCCESS && swapchain) {
        std::lock_guard<std::mutex> lock(g_mu);
        g_swapchains[*swapchain] =
            SwapchainInfo{patched_create_info.imageFormat, patched_create_info.imageExtent, {}};
        if (verbose_logging()) {
            std::fprintf(stderr, "[frame-capture] swapchain %ux%u fmt=%d usage=0x%x\n",
                         patched_create_info.imageExtent.width,
                         patched_create_info.imageExtent.height, patched_create_info.imageFormat,
                         patched_create_info.imageUsage);
        }
    }
    return result;
}

extern "C" VKAPI_ATTR void VKAPI_CALL
vkDestroySwapchainKHR(VkDevice device, VkSwapchainKHR swapchain,
                      const VkAllocationCallbacks *allocator) {
    {
        std::lock_guard<std::mutex> lock(g_mu);
        g_swapchains.erase(swapchain);
    }
    g_next_destroy_swapchain_khr(device, swapchain, allocator);
}

extern "C" VKAPI_ATTR VkResult VKAPI_CALL
vkGetSwapchainImagesKHR(VkDevice device, VkSwapchainKHR swapchain, uint32_t *count,
                        VkImage *images) {
    VkResult result = g_next_get_swapchain_images_khr(device, swapchain, count, images);
    if ((result == VK_SUCCESS || result == VK_INCOMPLETE) && images && count) {
        std::lock_guard<std::mutex> lock(g_mu);
        auto it = g_swapchains.find(swapchain);
        if (it != g_swapchains.end()) {
            it->second.images.assign(images, images + *count);
        }
    }
    return result;
}

extern "C" VKAPI_ATTR void VKAPI_CALL vkGetDeviceQueue(VkDevice device, uint32_t queue_family,
                                                       uint32_t queue_index, VkQueue *queue) {
    g_next_get_device_queue(device, queue_family, queue_index, queue);
    if (queue && *queue) {
        VkQueueFlags flags = queue_family < g_queue_family_flags.size()
                                 ? g_queue_family_flags[queue_family]
                                 : 0;
        std::lock_guard<std::mutex> lock(g_mu);
        g_queues[*queue] = QueueInfo{queue_family, flags};
    }
}

extern "C" VKAPI_ATTR VkResult VKAPI_CALL
vkQueuePresentKHR(VkQueue queue, const VkPresentInfoKHR *present_info) {
    if (!g_next_queue_present_khr || !present_info) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    bool captured = false;
    if (present_info->swapchainCount == 1 && present_info->pSwapchains &&
        present_info->pImageIndices) {
        captured = capture_present_image(queue, present_info->pSwapchains[0],
                                         present_info->pImageIndices[0],
                                         present_info->pWaitSemaphores,
                                         present_info->waitSemaphoreCount);
    } else if (present_info->swapchainCount != 0) {
        log_capture_failure("multi-swapchain present is unsupported");
    }

    if (!captured) {
        return g_next_queue_present_khr(queue, present_info);
    }

    VkPresentInfoKHR patched_present = *present_info;
    patched_present.waitSemaphoreCount = 0;
    patched_present.pWaitSemaphores = nullptr;
    return g_next_queue_present_khr(queue, &patched_present);
}

extern "C" VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice device,
                                                                        const char *name) {
    if (std::strcmp(name, "vkGetDeviceProcAddr") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(vkGetDeviceProcAddr);
    }
    if (std::strcmp(name, "vkCreateSwapchainKHR") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(vkCreateSwapchainKHR);
    }
    if (std::strcmp(name, "vkDestroySwapchainKHR") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(vkDestroySwapchainKHR);
    }
    if (std::strcmp(name, "vkGetSwapchainImagesKHR") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(vkGetSwapchainImagesKHR);
    }
    if (std::strcmp(name, "vkQueuePresentKHR") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(vkQueuePresentKHR);
    }
    if (std::strcmp(name, "vkGetDeviceQueue") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(vkGetDeviceQueue);
    }
    if (g_next_gdpa) {
        return g_next_gdpa(device, name);
    }
    return nullptr;
}

extern "C" VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance instance,
                                                                          const char *name) {
    if (std::strcmp(name, "vkGetInstanceProcAddr") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(vkGetInstanceProcAddr);
    }
    if (std::strcmp(name, "vkGetDeviceProcAddr") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(vkGetDeviceProcAddr);
    }
    if (std::strcmp(name, "vkCreateInstance") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(vkCreateInstance);
    }
    if (std::strcmp(name, "vkCreateDevice") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(vkCreateDevice);
    }
    if (std::strcmp(name, "vkCreateSwapchainKHR") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(vkCreateSwapchainKHR);
    }
    if (std::strcmp(name, "vkDestroySwapchainKHR") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(vkDestroySwapchainKHR);
    }
    if (std::strcmp(name, "vkGetSwapchainImagesKHR") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(vkGetSwapchainImagesKHR);
    }
    if (std::strcmp(name, "vkQueuePresentKHR") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(vkQueuePresentKHR);
    }
    if (g_next_gipa) {
        return g_next_gipa(instance, name);
    }
    return nullptr;
}
