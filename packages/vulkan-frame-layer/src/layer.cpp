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

struct ImageInfo {
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkExtent3D extent{};
    VkImageUsageFlags usage = 0;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
};

struct ImageViewInfo {
    VkImage image = VK_NULL_HANDLE;
};

struct RenderPassInfo {
    std::vector<VkImageLayout> final_layouts;
};

struct FramebufferInfo {
    VkRenderPass render_pass = VK_NULL_HANDLE;
    std::vector<VkImageView> attachments;
};

struct CmdState {
    VkRenderPass render_pass = VK_NULL_HANDLE;
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
};

struct ExportedResource {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
};

static PFN_vkGetInstanceProcAddr g_next_gipa = nullptr;
static PFN_vkGetDeviceProcAddr g_next_gdpa = nullptr;
static PFN_vkGetPhysicalDeviceMemoryProperties g_get_memory_props = nullptr;
static PFN_vkCreateImage g_next_create_image = nullptr;
static PFN_vkDestroyImage g_next_destroy_image = nullptr;
static PFN_vkCreateImageView g_next_create_image_view = nullptr;
static PFN_vkDestroyImageView g_next_destroy_image_view = nullptr;
static PFN_vkCreateRenderPass g_next_create_render_pass = nullptr;
static PFN_vkDestroyRenderPass g_next_destroy_render_pass = nullptr;
static PFN_vkCreateFramebuffer g_next_create_framebuffer = nullptr;
static PFN_vkDestroyFramebuffer g_next_destroy_framebuffer = nullptr;
static PFN_vkCmdPipelineBarrier g_next_cmd_pipeline_barrier = nullptr;
static PFN_vkCmdBeginRenderPass g_next_cmd_begin_render_pass = nullptr;
static PFN_vkCmdEndRenderPass g_next_cmd_end_render_pass = nullptr;
static PFN_vkGetDeviceQueue g_next_get_device_queue = nullptr;
static PFN_vkQueueSubmit g_next_queue_submit = nullptr;
static PFN_vkQueueSubmit2 g_next_queue_submit2 = nullptr;
static PFN_vkQueueSubmit2KHR g_next_queue_submit2_khr = nullptr;
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
static PFN_vkCmdCopyImageToBuffer g_next_cmd_copy_image_to_buffer = nullptr;

static VkDevice g_device = VK_NULL_HANDLE;
static uint32_t g_default_queue_family = 0;
static VkPhysicalDeviceMemoryProperties g_memory_props{};
static std::atomic<uint32_t> g_frame_id{0};
static std::atomic<uint32_t> g_submit_count{0};
static std::atomic<uint32_t> g_pending_requests{0};
static std::atomic<bool> g_server_started{false};
static std::atomic<uint32_t> g_copy_logs{0};
static std::atomic<uint32_t> g_export_fail_logs{0};
static std::mutex g_mu;
static std::mutex g_client_mu;
static std::mutex g_export_mu;
static int g_client_fd = -1;
static std::deque<ExportedResource> g_retained_exports;
static std::unordered_map<VkImage, ImageInfo> g_images;
static std::unordered_map<VkImageView, ImageViewInfo> g_image_views;
static std::unordered_map<VkRenderPass, RenderPassInfo> g_render_passes;
static std::unordered_map<VkFramebuffer, FramebufferInfo> g_framebuffers;
static std::unordered_map<VkCommandBuffer, CmdState> g_cmds;
static std::unordered_map<VkQueue, uint32_t> g_queue_families;

static uint32_t skip_submits() {
    const char *raw = std::getenv("FRAME_CAPTURE_SKIP_SUBMITS");
    if (!raw || !*raw) {
        return 300;
    }
    char *end = nullptr;
    unsigned long value = std::strtoul(raw, &end, 10);
    return end == raw ? 300 : static_cast<uint32_t>(value);
}

static bool verbose_logging() {
    const char *raw = std::getenv("FRAME_CAPTURE_VERBOSE");
    return raw && std::strcmp(raw, "0") != 0 && raw[0] != '\0';
}

static bool try_acquire_request() {
    uint32_t current = g_pending_requests.load();
    while (current > 0) {
        if (g_pending_requests.compare_exchange_weak(current, current - 1)) {
            return true;
        }
    }
    return false;
}

static void release_request() {
    g_pending_requests.fetch_add(1);
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

static bool passes_capture_size_filter(const ImageInfo &info) {
    const uint32_t want_w = env_u32("FRAME_CAPTURE_WIDTH", 608);
    const uint32_t want_h = env_u32("FRAME_CAPTURE_HEIGHT", 456);
    if (want_w == 0 || want_h == 0) {
        return true;
    }
    return info.extent.width == want_w && info.extent.height == want_h;
}

static bool is_supported_format(VkFormat format) {
    return format == VK_FORMAT_R8G8B8A8_UNORM || format == VK_FORMAT_R8G8B8A8_SRGB ||
           format == VK_FORMAT_B8G8R8A8_UNORM || format == VK_FORMAT_B8G8R8A8_SRGB;
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

static void retain_exported_resource(VkBuffer buffer, VkDeviceMemory memory) {
    const uint32_t retain_count = env_u32("FRAME_CAPTURE_RETAINED_EXPORTS", 16);
    std::lock_guard<std::mutex> lock(g_export_mu);
    g_retained_exports.push_back(ExportedResource{buffer, memory});
    while (retain_count > 0 && g_retained_exports.size() > retain_count) {
        destroy_exported_resource(g_retained_exports.front());
        g_retained_exports.pop_front();
    }
}

static void set_client_fd(int fd) {
    std::lock_guard<std::mutex> lock(g_client_mu);
    if (g_client_fd >= 0) {
        close(g_client_fd);
    }
    g_client_fd = fd;
}

static void clear_client_fd(int fd) {
    std::lock_guard<std::mutex> lock(g_client_mu);
    if (g_client_fd == fd) {
        g_client_fd = -1;
    }
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
        set_client_fd(client);

        char command = 0;
        while (read(client, &command, 1) == 1) {
            if (command == 'R') {
                g_pending_requests.fetch_add(1);
            } else if (command == 'Q') {
                break;
            }
        }

        clear_client_fd(client);
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

static bool send_external_frame_fd(uint32_t width, uint32_t height, uint32_t stride, uint32_t size,
                                   VkFormat format, int fd) {
    const uint32_t frame_id = g_frame_id.fetch_add(1);
    uint32_t header[6] = {width, height, stride, size, frame_id, static_cast<uint32_t>(format)};
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

    std::lock_guard<std::mutex> lock(g_client_mu);
    if (g_client_fd < 0) {
        return false;
    }
    if (sendmsg(g_client_fd, &msg, MSG_NOSIGNAL) < 0) {
        close(g_client_fd);
        g_client_fd = -1;
        return false;
    }
    return true;
}

static void load_device_functions(VkDevice device) {
    g_next_create_image = reinterpret_cast<PFN_vkCreateImage>(g_next_gdpa(device, "vkCreateImage"));
    g_next_destroy_image =
        reinterpret_cast<PFN_vkDestroyImage>(g_next_gdpa(device, "vkDestroyImage"));
    g_next_create_image_view =
        reinterpret_cast<PFN_vkCreateImageView>(g_next_gdpa(device, "vkCreateImageView"));
    g_next_destroy_image_view =
        reinterpret_cast<PFN_vkDestroyImageView>(g_next_gdpa(device, "vkDestroyImageView"));
    g_next_create_render_pass =
        reinterpret_cast<PFN_vkCreateRenderPass>(g_next_gdpa(device, "vkCreateRenderPass"));
    g_next_destroy_render_pass =
        reinterpret_cast<PFN_vkDestroyRenderPass>(g_next_gdpa(device, "vkDestroyRenderPass"));
    g_next_create_framebuffer =
        reinterpret_cast<PFN_vkCreateFramebuffer>(g_next_gdpa(device, "vkCreateFramebuffer"));
    g_next_destroy_framebuffer =
        reinterpret_cast<PFN_vkDestroyFramebuffer>(g_next_gdpa(device, "vkDestroyFramebuffer"));
    g_next_cmd_pipeline_barrier =
        reinterpret_cast<PFN_vkCmdPipelineBarrier>(g_next_gdpa(device, "vkCmdPipelineBarrier"));
    g_next_cmd_begin_render_pass =
        reinterpret_cast<PFN_vkCmdBeginRenderPass>(g_next_gdpa(device, "vkCmdBeginRenderPass"));
    g_next_cmd_end_render_pass =
        reinterpret_cast<PFN_vkCmdEndRenderPass>(g_next_gdpa(device, "vkCmdEndRenderPass"));
    g_next_get_device_queue =
        reinterpret_cast<PFN_vkGetDeviceQueue>(g_next_gdpa(device, "vkGetDeviceQueue"));
    g_next_queue_submit = reinterpret_cast<PFN_vkQueueSubmit>(g_next_gdpa(device, "vkQueueSubmit"));
    g_next_queue_submit2 =
        reinterpret_cast<PFN_vkQueueSubmit2>(g_next_gdpa(device, "vkQueueSubmit2"));
    g_next_queue_submit2_khr =
        reinterpret_cast<PFN_vkQueueSubmit2KHR>(g_next_gdpa(device, "vkQueueSubmit2KHR"));
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
    g_next_cmd_copy_image_to_buffer =
        reinterpret_cast<PFN_vkCmdCopyImageToBuffer>(g_next_gdpa(device, "vkCmdCopyImageToBuffer"));
}

static bool copy_image_to_export_fd(VkQueue queue, uint32_t family, VkImage image,
                                    const ImageInfo &info) {
    auto log_fail = [](const char *label, int code = 0) {
        if (g_export_fail_logs.fetch_add(1) < 40) {
            std::fprintf(stderr, "[frame-capture] export fd failed: %s code=%d\n", label, code);
        }
    };
    if (!g_device || !g_next_create_buffer || !g_next_cmd_copy_image_to_buffer ||
        !g_next_get_memory_fd_khr || !is_supported_format(info.format) || info.extent.width < 64 ||
        info.extent.height < 64 || info.extent.width > 4096 || info.extent.height > 4096) {
        log_fail("precheck");
        return false;
    }

    const uint32_t width = info.extent.width;
    const uint32_t height = info.extent.height;
    const uint32_t stride = width * 4;
    const VkDeviceSize size = static_cast<VkDeviceSize>(stride) * height;

    VkExternalMemoryBufferCreateInfo ext_buf{};
    ext_buf.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO;
    ext_buf.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;

    VkBufferCreateInfo buffer_ci{};
    buffer_ci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_ci.pNext = &ext_buf;
    buffer_ci.size = size;
    buffer_ci.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    buffer_ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkBuffer buffer = VK_NULL_HANDLE;
    VkResult rc = g_next_create_buffer(g_device, &buffer_ci, nullptr, &buffer);
    if (rc != VK_SUCCESS) {
        log_fail("vkCreateBuffer", rc);
        return false;
    }

    VkMemoryRequirements req{};
    g_next_get_buffer_memory_requirements(g_device, buffer, &req);
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
        if (g_export_fail_logs.fetch_add(1) < 40) {
            std::fprintf(stderr,
                         "[frame-capture] export fd failed: memoryType bits=0x%x reqSize=%llu\n",
                         req.memoryTypeBits, static_cast<unsigned long long>(req.size));
        }
        g_next_destroy_buffer(g_device, buffer, nullptr);
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

    VkDeviceMemory memory = VK_NULL_HANDLE;
    rc = g_next_allocate_memory(g_device, &alloc, nullptr, &memory);
    if (rc != VK_SUCCESS) {
        log_fail("vkAllocateMemory", rc);
        g_next_destroy_buffer(g_device, buffer, nullptr);
        return false;
    }
    rc = g_next_bind_buffer_memory(g_device, buffer, memory, 0);
    if (rc != VK_SUCCESS) {
        log_fail("vkBindBufferMemory", rc);
        destroy_buffer_memory(buffer, memory);
        return false;
    }

    VkCommandPoolCreateInfo pool_ci{};
    pool_ci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_ci.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    pool_ci.queueFamilyIndex = family;
    VkCommandPool pool = VK_NULL_HANDLE;
    rc = g_next_create_command_pool(g_device, &pool_ci, nullptr, &pool);
    if (rc != VK_SUCCESS) {
        log_fail("vkCreateCommandPool", rc);
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
        log_fail("vkAllocateCommandBuffers", rc);
        g_next_destroy_command_pool(g_device, pool, nullptr);
        destroy_buffer_memory(buffer, memory);
        return false;
    }

    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    rc = g_next_begin_command_buffer(cmd, &begin);
    if (rc != VK_SUCCESS) {
        log_fail("vkBeginCommandBuffer", rc);
        g_next_free_command_buffers(g_device, pool, 1, &cmd);
        g_next_destroy_command_pool(g_device, pool, nullptr);
        destroy_buffer_memory(buffer, memory);
        return false;
    }

    VkImageLayout assumed_layout = info.layout == VK_IMAGE_LAYOUT_UNDEFINED
                                       ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
                                       : info.layout;
    VkImageMemoryBarrier to_transfer{};
    to_transfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    to_transfer.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    to_transfer.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    to_transfer.oldLayout = assumed_layout;
    to_transfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    to_transfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_transfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_transfer.image = image;
    to_transfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    to_transfer.subresourceRange.levelCount = 1;
    to_transfer.subresourceRange.layerCount = 1;
    g_next_cmd_pipeline_barrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
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
    back.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    back.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    back.newLayout = assumed_layout;
    g_next_cmd_pipeline_barrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1,
                                &back);
    rc = g_next_end_command_buffer(cmd);
    if (rc != VK_SUCCESS) {
        log_fail("vkEndCommandBuffer", rc);
        g_next_free_command_buffers(g_device, pool, 1, &cmd);
        g_next_destroy_command_pool(g_device, pool, nullptr);
        destroy_buffer_memory(buffer, memory);
        return false;
    }

    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;
    rc = g_next_queue_submit(queue, 1, &submit, VK_NULL_HANDLE);
    if (rc != VK_SUCCESS) {
        log_fail("vkQueueSubmit(copy)", rc);
        g_next_free_command_buffers(g_device, pool, 1, &cmd);
        g_next_destroy_command_pool(g_device, pool, nullptr);
        destroy_buffer_memory(buffer, memory);
        return false;
    }
    g_next_queue_wait_idle(queue);

    VkMemoryGetFdInfoKHR fd_info{};
    fd_info.sType = VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR;
    fd_info.memory = memory;
    fd_info.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
    int fd = -1;
    rc = g_next_get_memory_fd_khr(g_device, &fd_info, &fd);
    if (rc != VK_SUCCESS || fd < 0) {
        log_fail("vkGetMemoryFdKHR", rc);
        g_next_free_command_buffers(g_device, pool, 1, &cmd);
        g_next_destroy_command_pool(g_device, pool, nullptr);
        destroy_buffer_memory(buffer, memory);
        return false;
    }

    bool sent =
        send_external_frame_fd(width, height, stride, static_cast<uint32_t>(size), info.format, fd);
    if (sent && verbose_logging()) {
        std::fprintf(stderr, "[frame-capture] exported 4ch fd frame %ux%u fmt=%d size=%u sent=%d\n",
                     width, height, info.format, static_cast<uint32_t>(size), 1);
    } else if (!sent && g_export_fail_logs.fetch_add(1) < 40) {
        std::fprintf(stderr, "[frame-capture] export fd failed: no active client for %ux%u\n",
                     width, height);
    }
    close(fd);

    g_next_free_command_buffers(g_device, pool, 1, &cmd);
    g_next_destroy_command_pool(g_device, pool, nullptr);
    if (sent) {
        retain_exported_resource(buffer, memory);
    } else {
        destroy_buffer_memory(buffer, memory);
    }
    return sent;
}

static void try_capture_real_frame(VkQueue queue) {
    if (g_pending_requests.load() == 0) {
        return;
    }

    std::vector<std::pair<VkImage, ImageInfo>> candidates;
    uint32_t family = g_default_queue_family;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        auto qit = g_queue_families.find(queue);
        if (qit != g_queue_families.end()) {
            family = qit->second;
        }
        for (const auto &[image, info] : g_images) {
            if (!(info.usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) ||
                !is_supported_format(info.format) || !passes_capture_size_filter(info)) {
                continue;
            }
            candidates.push_back({image, info});
        }
    }

    std::sort(candidates.begin(), candidates.end(), [](const auto &a, const auto &b) {
        uint64_t aa = uint64_t(a.second.extent.width) * a.second.extent.height;
        uint64_t bb = uint64_t(b.second.extent.width) * b.second.extent.height;
        return aa > bb;
    });

    for (const auto &[image, info] : candidates) {
        if (!try_acquire_request()) {
            return;
        }
        if (verbose_logging() && g_copy_logs.fetch_add(1) < 20) {
            std::fprintf(
                stderr,
                "[frame-capture] try copy candidate %ux%u fmt=%d usage=0x%x layout=%d family=%u\n",
                info.extent.width, info.extent.height, info.format, info.usage, info.layout,
                family);
        }
        if (!copy_image_to_export_fd(queue, family, image, info)) {
            release_request();
        }
    }
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
    auto next_create_device =
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

    VkResult result = next_create_device(physical_device, &patched_create_info, allocator, device);
    if (result == VK_SUCCESS) {
        g_device = *device;
        if (create_info->queueCreateInfoCount > 0) {
            g_default_queue_family = create_info->pQueueCreateInfos[0].queueFamilyIndex;
        }
        if (g_get_memory_props) {
            g_get_memory_props(physical_device, &g_memory_props);
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

extern "C" VKAPI_ATTR VkResult VKAPI_CALL vkCreateImage(VkDevice device,
                                                        const VkImageCreateInfo *create_info,
                                                        const VkAllocationCallbacks *allocator,
                                                        VkImage *image) {
    VkResult result = g_next_create_image(device, create_info, allocator, image);
    if (result == VK_SUCCESS && image && create_info) {
        ImageInfo info;
        info.format = create_info->format;
        info.extent = create_info->extent;
        info.usage = create_info->usage;
        info.layout = create_info->initialLayout;
        std::lock_guard<std::mutex> lock(g_mu);
        g_images[*image] = info;
        if (verbose_logging() && (info.usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) &&
            is_supported_format(info.format) && info.extent.width >= 64 &&
            info.extent.height >= 64) {
            std::fprintf(
                stderr, "[frame-capture] image candidate %ux%u format=%d usage=0x%x layout=%d\n",
                info.extent.width, info.extent.height, info.format, info.usage, info.layout);
        }
    }
    return result;
}

extern "C" VKAPI_ATTR void VKAPI_CALL vkDestroyImage(VkDevice device, VkImage image,
                                                     const VkAllocationCallbacks *allocator) {
    {
        std::lock_guard<std::mutex> lock(g_mu);
        g_images.erase(image);
    }
    g_next_destroy_image(device, image, allocator);
}

extern "C" VKAPI_ATTR VkResult VKAPI_CALL
vkCreateImageView(VkDevice device, const VkImageViewCreateInfo *create_info,
                  const VkAllocationCallbacks *allocator, VkImageView *view) {
    VkResult result = g_next_create_image_view(device, create_info, allocator, view);
    if (result == VK_SUCCESS && create_info && view) {
        std::lock_guard<std::mutex> lock(g_mu);
        g_image_views[*view] = ImageViewInfo{create_info->image};
    }
    return result;
}

extern "C" VKAPI_ATTR void VKAPI_CALL vkDestroyImageView(VkDevice device, VkImageView view,
                                                         const VkAllocationCallbacks *allocator) {
    {
        std::lock_guard<std::mutex> lock(g_mu);
        g_image_views.erase(view);
    }
    g_next_destroy_image_view(device, view, allocator);
}

extern "C" VKAPI_ATTR VkResult VKAPI_CALL
vkCreateRenderPass(VkDevice device, const VkRenderPassCreateInfo *create_info,
                   const VkAllocationCallbacks *allocator, VkRenderPass *render_pass) {
    VkResult result = g_next_create_render_pass(device, create_info, allocator, render_pass);
    if (result == VK_SUCCESS && create_info && render_pass) {
        RenderPassInfo info;
        for (uint32_t i = 0; i < create_info->attachmentCount; ++i) {
            info.final_layouts.push_back(create_info->pAttachments[i].finalLayout);
        }
        std::lock_guard<std::mutex> lock(g_mu);
        g_render_passes[*render_pass] = std::move(info);
    }
    return result;
}

extern "C" VKAPI_ATTR void VKAPI_CALL vkDestroyRenderPass(VkDevice device, VkRenderPass render_pass,
                                                          const VkAllocationCallbacks *allocator) {
    {
        std::lock_guard<std::mutex> lock(g_mu);
        g_render_passes.erase(render_pass);
    }
    g_next_destroy_render_pass(device, render_pass, allocator);
}

extern "C" VKAPI_ATTR VkResult VKAPI_CALL
vkCreateFramebuffer(VkDevice device, const VkFramebufferCreateInfo *create_info,
                    const VkAllocationCallbacks *allocator, VkFramebuffer *framebuffer) {
    VkResult result = g_next_create_framebuffer(device, create_info, allocator, framebuffer);
    if (result == VK_SUCCESS && create_info && framebuffer) {
        FramebufferInfo info;
        info.render_pass = create_info->renderPass;
        for (uint32_t i = 0; i < create_info->attachmentCount; ++i) {
            info.attachments.push_back(create_info->pAttachments[i]);
        }
        std::lock_guard<std::mutex> lock(g_mu);
        g_framebuffers[*framebuffer] = std::move(info);
    }
    return result;
}

extern "C" VKAPI_ATTR void VKAPI_CALL vkDestroyFramebuffer(VkDevice device,
                                                           VkFramebuffer framebuffer,
                                                           const VkAllocationCallbacks *allocator) {
    {
        std::lock_guard<std::mutex> lock(g_mu);
        g_framebuffers.erase(framebuffer);
    }
    g_next_destroy_framebuffer(device, framebuffer, allocator);
}

extern "C" VKAPI_ATTR void VKAPI_CALL vkCmdPipelineBarrier(
    VkCommandBuffer cmd, VkPipelineStageFlags src_stage, VkPipelineStageFlags dst_stage,
    VkDependencyFlags deps, uint32_t mem_count, const VkMemoryBarrier *mem_barriers,
    uint32_t buf_count, const VkBufferMemoryBarrier *buf_barriers, uint32_t img_count,
    const VkImageMemoryBarrier *img_barriers) {
    {
        std::lock_guard<std::mutex> lock(g_mu);
        for (uint32_t i = 0; i < img_count; ++i) {
            auto it = g_images.find(img_barriers[i].image);
            if (it != g_images.end()) {
                it->second.layout = img_barriers[i].newLayout;
            }
        }
    }
    g_next_cmd_pipeline_barrier(cmd, src_stage, dst_stage, deps, mem_count, mem_barriers, buf_count,
                                buf_barriers, img_count, img_barriers);
}

extern "C" VKAPI_ATTR void VKAPI_CALL vkCmdBeginRenderPass(VkCommandBuffer cmd,
                                                           const VkRenderPassBeginInfo *begin_info,
                                                           VkSubpassContents contents) {
    {
        std::lock_guard<std::mutex> lock(g_mu);
        g_cmds[cmd] = CmdState{begin_info->renderPass, begin_info->framebuffer};
    }
    g_next_cmd_begin_render_pass(cmd, begin_info, contents);
}

extern "C" VKAPI_ATTR void VKAPI_CALL vkCmdEndRenderPass(VkCommandBuffer cmd) {
    g_next_cmd_end_render_pass(cmd);
    std::lock_guard<std::mutex> lock(g_mu);
    auto cmd_it = g_cmds.find(cmd);
    if (cmd_it == g_cmds.end()) {
        return;
    }
    auto fb_it = g_framebuffers.find(cmd_it->second.framebuffer);
    auto rp_it = g_render_passes.find(cmd_it->second.render_pass);
    if (fb_it != g_framebuffers.end() && rp_it != g_render_passes.end()) {
        const size_t n =
            std::min(fb_it->second.attachments.size(), rp_it->second.final_layouts.size());
        for (size_t i = 0; i < n; ++i) {
            auto view_it = g_image_views.find(fb_it->second.attachments[i]);
            if (view_it == g_image_views.end()) {
                continue;
            }
            auto image_it = g_images.find(view_it->second.image);
            if (image_it != g_images.end()) {
                image_it->second.layout = rp_it->second.final_layouts[i];
            }
        }
    }
    g_cmds.erase(cmd_it);
}

extern "C" VKAPI_ATTR void VKAPI_CALL vkGetDeviceQueue(VkDevice device, uint32_t queue_family,
                                                       uint32_t queue_index, VkQueue *queue) {
    g_next_get_device_queue(device, queue_family, queue_index, queue);
    if (queue && *queue) {
        std::lock_guard<std::mutex> lock(g_mu);
        g_queue_families[*queue] = queue_family;
    }
}

extern "C" VKAPI_ATTR VkResult VKAPI_CALL vkQueueSubmit(VkQueue queue, uint32_t submit_count,
                                                        const VkSubmitInfo *submits,
                                                        VkFence fence) {
    if (verbose_logging()) {
        std::fprintf(stderr, "[frame-capture] vkQueueSubmit count=%u\n", submit_count);
    }
    VkResult result = g_next_queue_submit(queue, submit_count, submits, fence);
    if (result == VK_SUCCESS && g_next_queue_wait_idle) {
        if (g_submit_count.fetch_add(1) < skip_submits()) {
            return result;
        }
        g_next_queue_wait_idle(queue);
        try_capture_real_frame(queue);
    }
    return result;
}

extern "C" VKAPI_ATTR VkResult VKAPI_CALL vkQueueSubmit2(VkQueue queue, uint32_t submit_count,
                                                         const VkSubmitInfo2 *submits,
                                                         VkFence fence) {
    if (verbose_logging()) {
        std::fprintf(stderr, "[frame-capture] vkQueueSubmit2 count=%u\n", submit_count);
    }
    if (!g_next_queue_submit2) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    VkResult result = g_next_queue_submit2(queue, submit_count, submits, fence);
    if (result == VK_SUCCESS && g_next_queue_wait_idle) {
        if (g_submit_count.fetch_add(1) < skip_submits()) {
            return result;
        }
        g_next_queue_wait_idle(queue);
        try_capture_real_frame(queue);
    }
    return result;
}

extern "C" VKAPI_ATTR VkResult VKAPI_CALL vkQueueSubmit2KHR(VkQueue queue, uint32_t submit_count,
                                                            const VkSubmitInfo2 *submits,
                                                            VkFence fence) {
    if (verbose_logging()) {
        std::fprintf(stderr, "[frame-capture] vkQueueSubmit2KHR count=%u\n", submit_count);
    }
    PFN_vkQueueSubmit2KHR next =
        g_next_queue_submit2_khr ? g_next_queue_submit2_khr
                                 : reinterpret_cast<PFN_vkQueueSubmit2KHR>(g_next_queue_submit2);
    if (!next) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    VkResult result = next(queue, submit_count, submits, fence);
    if (result == VK_SUCCESS && g_next_queue_wait_idle) {
        if (g_submit_count.fetch_add(1) < skip_submits()) {
            return result;
        }
        g_next_queue_wait_idle(queue);
        try_capture_real_frame(queue);
    }
    return result;
}

extern "C" VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice device,
                                                                        const char *name) {
    if (std::strcmp(name, "vkGetDeviceProcAddr") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(vkGetDeviceProcAddr);
    }
    if (std::strcmp(name, "vkCreateImage") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(vkCreateImage);
    }
    if (std::strcmp(name, "vkDestroyImage") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(vkDestroyImage);
    }
    if (std::strcmp(name, "vkCreateImageView") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(vkCreateImageView);
    }
    if (std::strcmp(name, "vkDestroyImageView") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(vkDestroyImageView);
    }
    if (std::strcmp(name, "vkCreateRenderPass") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(vkCreateRenderPass);
    }
    if (std::strcmp(name, "vkDestroyRenderPass") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(vkDestroyRenderPass);
    }
    if (std::strcmp(name, "vkCreateFramebuffer") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(vkCreateFramebuffer);
    }
    if (std::strcmp(name, "vkDestroyFramebuffer") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(vkDestroyFramebuffer);
    }
    if (std::strcmp(name, "vkCmdPipelineBarrier") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(vkCmdPipelineBarrier);
    }
    if (std::strcmp(name, "vkCmdBeginRenderPass") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(vkCmdBeginRenderPass);
    }
    if (std::strcmp(name, "vkCmdEndRenderPass") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(vkCmdEndRenderPass);
    }
    if (std::strcmp(name, "vkGetDeviceQueue") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(vkGetDeviceQueue);
    }
    if (std::strcmp(name, "vkQueueSubmit") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(vkQueueSubmit);
    }
    if (std::strcmp(name, "vkQueueSubmit2") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(vkQueueSubmit2);
    }
    if (std::strcmp(name, "vkQueueSubmit2KHR") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(vkQueueSubmit2KHR);
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
    if (std::strcmp(name, "vkQueueSubmit") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(vkQueueSubmit);
    }
    if (std::strcmp(name, "vkQueueSubmit2") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(vkQueueSubmit2);
    }
    if (std::strcmp(name, "vkQueueSubmit2KHR") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(vkQueueSubmit2KHR);
    }
    if (g_next_gipa) {
        return g_next_gipa(instance, name);
    }
    return nullptr;
}
