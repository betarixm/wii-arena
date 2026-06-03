#include <vulkan/vk_layer.h>
#include <vulkan/vulkan.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

extern "C" VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance instance,
                                                                          const char *name);
extern "C" VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice device,
                                                                        const char *name);

struct ExportSlot {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceSize allocation_size = 0;
    VkDeviceSize payload_size = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t stride = 0;
    VkFormat format = VK_FORMAT_UNDEFINED;
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

struct ImageInfo {
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkExtent3D extent{};
    VkImageUsageFlags usage = 0;
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
    std::string name;
};

struct ImageViewInfo {
    VkImage image = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkImageAspectFlags aspect = 0;
};

struct AttachmentInfo {
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkImageLayout initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImageLayout final_layout = VK_IMAGE_LAYOUT_UNDEFINED;
};

struct SubpassInfo {
    std::vector<uint32_t> color_attachments;
    uint32_t depth_stencil_attachment = VK_ATTACHMENT_UNUSED;
};

struct RenderPassInfo {
    std::vector<AttachmentInfo> attachments;
    std::vector<SubpassInfo> subpasses;
};

struct FramebufferInfo {
    VkRenderPass render_pass = VK_NULL_HANDLE;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t layers = 0;
    std::vector<VkImage> attachments;
};

struct CommandBufferRenderPassInfo {
    VkRenderPass render_pass = VK_NULL_HANDLE;
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    uint32_t subpass = 0;
};

struct DescriptorSetInfo {
    std::vector<VkImage> sampled_images;
    std::unordered_map<uint32_t, VkImage> sampled_images_by_binding;
};

struct CommandBufferDescriptorInfo {
    std::vector<VkImage> sampled_images;
    std::unordered_map<uint32_t, VkImage> sampled_images_by_binding;
};

struct PendingXfbCapture {
    VkImage image = VK_NULL_HANDLE;
};

static PFN_vkGetInstanceProcAddr g_next_gipa = nullptr;
static PFN_vkGetDeviceProcAddr g_next_gdpa = nullptr;
static PFN_vkGetPhysicalDeviceMemoryProperties g_get_memory_props = nullptr;

static PFN_vkCreateDevice g_next_create_device = nullptr;
static PFN_vkCreateSwapchainKHR g_next_create_swapchain_khr = nullptr;
static PFN_vkDestroySwapchainKHR g_next_destroy_swapchain_khr = nullptr;
static PFN_vkGetSwapchainImagesKHR g_next_get_swapchain_images_khr = nullptr;
static PFN_vkQueuePresentKHR g_next_queue_present_khr = nullptr;
static PFN_vkQueueSubmit g_next_queue_submit = nullptr;
static PFN_vkQueueWaitIdle g_next_queue_wait_idle = nullptr;
static PFN_vkCreateImage g_next_create_image = nullptr;
static PFN_vkDestroyImage g_next_destroy_image = nullptr;
static PFN_vkCreateImageView g_next_create_image_view = nullptr;
static PFN_vkDestroyImageView g_next_destroy_image_view = nullptr;
static PFN_vkCreateFramebuffer g_next_create_framebuffer = nullptr;
static PFN_vkDestroyFramebuffer g_next_destroy_framebuffer = nullptr;
static PFN_vkCreateRenderPass g_next_create_render_pass = nullptr;
static PFN_vkDestroyRenderPass g_next_destroy_render_pass = nullptr;
static PFN_vkCreateBuffer g_next_create_buffer = nullptr;
static PFN_vkDestroyBuffer g_next_destroy_buffer = nullptr;
static PFN_vkGetBufferMemoryRequirements g_next_get_buffer_memory_requirements = nullptr;
static PFN_vkAllocateMemory g_next_allocate_memory = nullptr;
static PFN_vkFreeMemory g_next_free_memory = nullptr;
static PFN_vkBindBufferMemory g_next_bind_buffer_memory = nullptr;
static PFN_vkGetMemoryFdKHR g_next_get_memory_fd_khr = nullptr;
static PFN_vkFreeCommandBuffers g_next_free_command_buffers = nullptr;
static PFN_vkBeginCommandBuffer g_next_begin_command_buffer = nullptr;
static PFN_vkEndCommandBuffer g_next_end_command_buffer = nullptr;
static PFN_vkCmdBeginRenderPass g_next_cmd_begin_render_pass = nullptr;
static PFN_vkCmdEndRenderPass g_next_cmd_end_render_pass = nullptr;
static PFN_vkUpdateDescriptorSets g_next_update_descriptor_sets = nullptr;
static PFN_vkCmdBindDescriptorSets g_next_cmd_bind_descriptor_sets = nullptr;
static PFN_vkCmdDraw g_next_cmd_draw = nullptr;
static PFN_vkCmdDrawIndexed g_next_cmd_draw_indexed = nullptr;
static PFN_vkCmdPipelineBarrier g_next_cmd_pipeline_barrier = nullptr;
static PFN_vkCmdCopyImageToBuffer g_next_cmd_copy_image_to_buffer = nullptr;
static PFN_vkSetDebugUtilsObjectNameEXT g_next_set_debug_utils_object_name_ext = nullptr;

static VkDevice g_device = VK_NULL_HANDLE;
static VkPhysicalDeviceMemoryProperties g_memory_props{};

static std::atomic<bool> g_server_started{false};
static std::atomic<uint32_t> g_frame_id{0};
static std::atomic<uint32_t> g_capture_fail_logs{0};
static std::mutex g_mu;
static std::mutex g_export_mu;
static std::mutex g_status_mu;
static std::unordered_map<VkImage, ImageInfo> g_images;
static std::unordered_map<VkImageView, ImageViewInfo> g_image_views;
static std::unordered_map<VkFramebuffer, FramebufferInfo> g_framebuffers;
static std::unordered_map<VkRenderPass, RenderPassInfo> g_render_passes;
static std::unordered_map<VkCommandBuffer, CommandBufferRenderPassInfo> g_active_render_passes;
static std::unordered_set<VkCommandBuffer> g_pending_inline_capture_command_buffers;
static std::unordered_map<VkDescriptorSet, DescriptorSetInfo> g_descriptor_sets;
static std::unordered_map<VkCommandBuffer, CommandBufferDescriptorInfo>
    g_command_buffer_descriptors;
static std::unordered_map<VkCommandBuffer, PendingXfbCapture> g_pending_xfb_captures;
static std::unordered_map<VkSwapchainKHR, SwapchainInfo> g_swapchains;
static std::vector<ExportSlot> g_export_slots;
static uint32_t g_next_export_slot = 0;
static CapturedFrame g_latest_frame;
static std::string g_last_capture_status = "no captured frame yet";

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

static VkAccessFlags access_mask_for_layout(VkImageLayout layout) {
    switch (layout) {
    case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
        return VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
        return VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
               VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
        return VK_ACCESS_SHADER_READ_BIT;
    case VK_IMAGE_LAYOUT_GENERAL:
        return VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
        return VK_ACCESS_TRANSFER_READ_BIT;
    case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
        return VK_ACCESS_TRANSFER_WRITE_BIT;
    case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
    case VK_IMAGE_LAYOUT_UNDEFINED:
    default:
        return 0;
    }
}

static VkPipelineStageFlags stage_for_layout(VkImageLayout layout) {
    switch (layout) {
    case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
        return VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
        return VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
               VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
        return VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    case VK_IMAGE_LAYOUT_GENERAL:
        return VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
    case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
        return VK_PIPELINE_STAGE_TRANSFER_BIT;
    case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
    case VK_IMAGE_LAYOUT_UNDEFINED:
    default:
        return VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    }
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

static void set_capture_status(const char *label, int code = 0) {
    char buffer[256];
    if (code != 0) {
        std::snprintf(buffer, sizeof(buffer), "%s code=%d", label, code);
    } else {
        std::snprintf(buffer, sizeof(buffer), "%s", label);
    }
    std::lock_guard<std::mutex> lock(g_status_mu);
    g_last_capture_status = buffer;
}

static void log_capture_failure(const char *label, int code = 0) {
    set_capture_status(label, code);
    if (g_capture_fail_logs.fetch_add(1) < 80) {
        std::fprintf(stderr, "[frame-capture] %s code=%d\n", label, code);
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

static bool send_no_frame(int client) {
    std::string status;
    {
        std::lock_guard<std::mutex> lock(g_status_mu);
        status = g_last_capture_status;
    }
    if (status.empty()) {
        status = "no captured frame yet";
    }
    std::string payload = "N" + status;
    return write(client, payload.data(), payload.size()) == static_cast<ssize_t>(payload.size());
}

static bool send_latest_frame_fd(int client) {
    std::lock_guard<std::mutex> lock(g_export_mu);
    if (g_latest_frame.memory == VK_NULL_HANDLE || !g_next_get_memory_fd_khr) {
        return send_no_frame(client);
    }

    VkMemoryGetFdInfoKHR fd_info{};
    fd_info.sType = VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR;
    fd_info.memory = g_latest_frame.memory;
    fd_info.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
    int fd = -1;
    VkResult rc = g_next_get_memory_fd_khr(g_device, &fd_info, &fd);
    if (rc != VK_SUCCESS || fd < 0) {
        log_capture_failure("export latest fd failed", rc);
        return send_no_frame(client);
    }

    uint32_t header[6] = {
        g_latest_frame.width, g_latest_frame.height,   g_latest_frame.stride,
        g_latest_frame.size,  g_latest_frame.frame_id, static_cast<uint32_t>(g_latest_frame.format),
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
    g_next_create_swapchain_khr =
        reinterpret_cast<PFN_vkCreateSwapchainKHR>(g_next_gdpa(device, "vkCreateSwapchainKHR"));
    g_next_destroy_swapchain_khr =
        reinterpret_cast<PFN_vkDestroySwapchainKHR>(g_next_gdpa(device, "vkDestroySwapchainKHR"));
    g_next_get_swapchain_images_khr = reinterpret_cast<PFN_vkGetSwapchainImagesKHR>(
        g_next_gdpa(device, "vkGetSwapchainImagesKHR"));
    g_next_queue_present_khr =
        reinterpret_cast<PFN_vkQueuePresentKHR>(g_next_gdpa(device, "vkQueuePresentKHR"));
    g_next_queue_submit = reinterpret_cast<PFN_vkQueueSubmit>(g_next_gdpa(device, "vkQueueSubmit"));
    g_next_queue_wait_idle =
        reinterpret_cast<PFN_vkQueueWaitIdle>(g_next_gdpa(device, "vkQueueWaitIdle"));
    g_next_create_image = reinterpret_cast<PFN_vkCreateImage>(g_next_gdpa(device, "vkCreateImage"));
    g_next_destroy_image =
        reinterpret_cast<PFN_vkDestroyImage>(g_next_gdpa(device, "vkDestroyImage"));
    g_next_create_image_view =
        reinterpret_cast<PFN_vkCreateImageView>(g_next_gdpa(device, "vkCreateImageView"));
    g_next_destroy_image_view =
        reinterpret_cast<PFN_vkDestroyImageView>(g_next_gdpa(device, "vkDestroyImageView"));
    g_next_create_framebuffer =
        reinterpret_cast<PFN_vkCreateFramebuffer>(g_next_gdpa(device, "vkCreateFramebuffer"));
    g_next_destroy_framebuffer =
        reinterpret_cast<PFN_vkDestroyFramebuffer>(g_next_gdpa(device, "vkDestroyFramebuffer"));
    g_next_create_render_pass =
        reinterpret_cast<PFN_vkCreateRenderPass>(g_next_gdpa(device, "vkCreateRenderPass"));
    g_next_destroy_render_pass =
        reinterpret_cast<PFN_vkDestroyRenderPass>(g_next_gdpa(device, "vkDestroyRenderPass"));
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
    g_next_free_command_buffers =
        reinterpret_cast<PFN_vkFreeCommandBuffers>(g_next_gdpa(device, "vkFreeCommandBuffers"));
    g_next_begin_command_buffer =
        reinterpret_cast<PFN_vkBeginCommandBuffer>(g_next_gdpa(device, "vkBeginCommandBuffer"));
    g_next_end_command_buffer =
        reinterpret_cast<PFN_vkEndCommandBuffer>(g_next_gdpa(device, "vkEndCommandBuffer"));
    g_next_cmd_begin_render_pass =
        reinterpret_cast<PFN_vkCmdBeginRenderPass>(g_next_gdpa(device, "vkCmdBeginRenderPass"));
    g_next_cmd_end_render_pass =
        reinterpret_cast<PFN_vkCmdEndRenderPass>(g_next_gdpa(device, "vkCmdEndRenderPass"));
    g_next_update_descriptor_sets =
        reinterpret_cast<PFN_vkUpdateDescriptorSets>(g_next_gdpa(device, "vkUpdateDescriptorSets"));
    g_next_cmd_bind_descriptor_sets = reinterpret_cast<PFN_vkCmdBindDescriptorSets>(
        g_next_gdpa(device, "vkCmdBindDescriptorSets"));
    g_next_cmd_draw = reinterpret_cast<PFN_vkCmdDraw>(g_next_gdpa(device, "vkCmdDraw"));
    g_next_cmd_draw_indexed =
        reinterpret_cast<PFN_vkCmdDrawIndexed>(g_next_gdpa(device, "vkCmdDrawIndexed"));
    g_next_cmd_pipeline_barrier =
        reinterpret_cast<PFN_vkCmdPipelineBarrier>(g_next_gdpa(device, "vkCmdPipelineBarrier"));
    g_next_cmd_copy_image_to_buffer =
        reinterpret_cast<PFN_vkCmdCopyImageToBuffer>(g_next_gdpa(device, "vkCmdCopyImageToBuffer"));
    if (!g_next_set_debug_utils_object_name_ext) {
        g_next_set_debug_utils_object_name_ext = reinterpret_cast<PFN_vkSetDebugUtilsObjectNameEXT>(
            g_next_gdpa(device, "vkSetDebugUtilsObjectNameEXT"));
    }
}

static bool create_export_buffer(VkDeviceSize payload_size, VkBuffer *buffer,
                                 VkDeviceMemory *memory, VkDeviceSize *allocation_size) {
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

static void clear_latest_if_matches_locked(VkBuffer buffer, VkDeviceMemory memory) {
    if (g_latest_frame.buffer == buffer || g_latest_frame.memory == memory) {
        g_latest_frame = CapturedFrame{};
    }
}

static void destroy_export_slot_locked(ExportSlot *slot) {
    if (!slot) {
        return;
    }
    clear_latest_if_matches_locked(slot->buffer, slot->memory);
    destroy_buffer_memory(slot->buffer, slot->memory);
    *slot = ExportSlot{};
}

static ExportSlot *acquire_export_slot_locked(VkDeviceSize payload_size, uint32_t width,
                                              uint32_t height, uint32_t stride, VkFormat format) {
    uint32_t ring_size = env_u32("FRAME_CAPTURE_EXPORT_RING_SIZE", 3);
    if (ring_size == 0) {
        ring_size = 1;
    }

    if (g_export_slots.size() > ring_size) {
        for (size_t i = ring_size; i < g_export_slots.size(); ++i) {
            destroy_export_slot_locked(&g_export_slots[i]);
        }
    }
    g_export_slots.resize(ring_size);
    g_next_export_slot %= ring_size;

    ExportSlot &slot = g_export_slots[g_next_export_slot];
    g_next_export_slot = (g_next_export_slot + 1) % ring_size;

    const bool reusable = slot.buffer != VK_NULL_HANDLE && slot.memory != VK_NULL_HANDLE &&
                          slot.payload_size == payload_size && slot.width == width &&
                          slot.height == height && slot.stride == stride && slot.format == format;
    if (reusable) {
        return &slot;
    }

    destroy_export_slot_locked(&slot);

    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceSize allocation_size = 0;
    if (!create_export_buffer(payload_size, &buffer, &memory, &allocation_size)) {
        return nullptr;
    }

    slot.buffer = buffer;
    slot.memory = memory;
    slot.allocation_size = allocation_size;
    slot.payload_size = payload_size;
    slot.width = width;
    slot.height = height;
    slot.stride = stride;
    slot.format = format;
    return &slot;
}

static bool record_inline_capture_to_latest(VkCommandBuffer command_buffer, VkImage image,
                                            const ImageInfo &info, const char *label) {
    if (!g_device || !g_next_cmd_copy_image_to_buffer || !g_next_cmd_pipeline_barrier) {
        log_capture_failure("inline capture unavailable");
        return false;
    }
    if (!is_supported_format(info.format)) {
        log_capture_failure("unsupported inline capture image format",
                            static_cast<int>(info.format));
        return false;
    }
    if (info.samples != VK_SAMPLE_COUNT_1_BIT) {
        log_capture_failure("inline capture image is multisampled", static_cast<int>(info.samples));
        return false;
    }
    if (info.layout == VK_IMAGE_LAYOUT_UNDEFINED) {
        log_capture_failure("inline capture image layout unknown");
        return false;
    }

    const uint32_t width = info.extent.width;
    const uint32_t height = info.extent.height;
    const uint32_t stride = width * 4;
    const VkDeviceSize payload_size = static_cast<VkDeviceSize>(stride) * height;

    ExportSlot slot_snapshot{};
    {
        std::lock_guard<std::mutex> lock(g_export_mu);
        ExportSlot *slot =
            acquire_export_slot_locked(payload_size, width, height, stride, info.format);
        if (!slot) {
            return false;
        }
        slot_snapshot = *slot;
    }

    const VkImageLayout old_layout = info.layout;
    VkImageMemoryBarrier to_transfer{};
    to_transfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    to_transfer.srcAccessMask = access_mask_for_layout(old_layout);
    to_transfer.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    to_transfer.oldLayout = old_layout;
    to_transfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    to_transfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_transfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_transfer.image = image;
    to_transfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    to_transfer.subresourceRange.levelCount = 1;
    to_transfer.subresourceRange.layerCount = 1;
    g_next_cmd_pipeline_barrier(command_buffer, stage_for_layout(old_layout),
                                VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                                &to_transfer);

    VkBufferImageCopy region{};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = {width, height, 1};
    g_next_cmd_copy_image_to_buffer(command_buffer, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                    slot_snapshot.buffer, 1, &region);

    VkBufferMemoryBarrier buffer_visible{};
    buffer_visible.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    buffer_visible.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    buffer_visible.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    buffer_visible.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    buffer_visible.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    buffer_visible.buffer = slot_snapshot.buffer;
    buffer_visible.offset = 0;
    buffer_visible.size = payload_size;
    g_next_cmd_pipeline_barrier(command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 1,
                                &buffer_visible, 0, nullptr);

    VkImageMemoryBarrier back = to_transfer;
    back.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    back.dstAccessMask = access_mask_for_layout(old_layout);
    back.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    back.newLayout = old_layout;
    g_next_cmd_pipeline_barrier(command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                stage_for_layout(old_layout), 0, 0, nullptr, 0, nullptr, 1, &back);

    {
        std::lock_guard<std::mutex> lock(g_export_mu);
        g_latest_frame = CapturedFrame{
            slot_snapshot.buffer,
            slot_snapshot.memory,
            width,
            height,
            stride,
            static_cast<uint32_t>(slot_snapshot.allocation_size),
            g_frame_id.fetch_add(1),
            info.format,
        };
    }
    {
        std::lock_guard<std::mutex> lock(g_mu);
        g_pending_inline_capture_command_buffers.insert(command_buffer);
    }
    set_capture_status("inline XFB source capture recorded");
    if (verbose_logging()) {
        std::fprintf(stderr,
                     "[frame-capture] recorded inline %s capture %ux%u fmt=%d layout=%d size=%u\n",
                     label, width, height, info.format, old_layout,
                     static_cast<uint32_t>(slot_snapshot.allocation_size));
    }
    return true;
}

static bool is_swapchain_image_locked(VkImage image) {
    if (image == VK_NULL_HANDLE) {
        return false;
    }
    for (const auto &swapchain : g_swapchains) {
        for (VkImage swapchain_image : swapchain.second.images) {
            if (swapchain_image == image) {
                return true;
            }
        }
    }
    return false;
}

static bool active_render_pass_targets_swapchain_locked(VkCommandBuffer command_buffer) {
    auto active_it = g_active_render_passes.find(command_buffer);
    if (active_it == g_active_render_passes.end()) {
        return false;
    }

    auto framebuffer_it = g_framebuffers.find(active_it->second.framebuffer);
    auto render_pass_it = g_render_passes.find(active_it->second.render_pass);
    if (framebuffer_it == g_framebuffers.end() || render_pass_it == g_render_passes.end()) {
        return false;
    }
    const RenderPassInfo &render_pass = render_pass_it->second;
    if (active_it->second.subpass >= render_pass.subpasses.size()) {
        return false;
    }

    const SubpassInfo &subpass = render_pass.subpasses[active_it->second.subpass];
    if (subpass.depth_stencil_attachment != VK_ATTACHMENT_UNUSED ||
        subpass.color_attachments.size() != 1) {
        return false;
    }

    const uint32_t color_index = subpass.color_attachments.front();
    return color_index < framebuffer_it->second.attachments.size() &&
           is_swapchain_image_locked(framebuffer_it->second.attachments[color_index]);
}

static bool find_bound_xfb_source_image_locked(VkCommandBuffer command_buffer, VkImage *image,
                                               ImageInfo *info) {
    auto descriptors_it = g_command_buffer_descriptors.find(command_buffer);
    if (descriptors_it == g_command_buffer_descriptors.end()) {
        return false;
    }

    auto binding_zero_it = descriptors_it->second.sampled_images_by_binding.find(0);
    if (binding_zero_it == descriptors_it->second.sampled_images_by_binding.end()) {
        return false;
    }

    auto image_it = g_images.find(binding_zero_it->second);
    if (image_it == g_images.end()) {
        return false;
    }
    const ImageInfo &candidate = image_it->second;
    if (!is_supported_format(candidate.format) || !(candidate.usage & VK_IMAGE_USAGE_SAMPLED_BIT)) {
        return false;
    }

    *image = binding_zero_it->second;
    *info = candidate;
    return true;
}

static void remember_bound_xfb_source(VkCommandBuffer command_buffer) {
    VkImage image = VK_NULL_HANDLE;
    ImageInfo info{};
    {
        std::lock_guard<std::mutex> lock(g_mu);
        if (g_pending_xfb_captures.find(command_buffer) != g_pending_xfb_captures.end()) {
            return;
        }
        if (!active_render_pass_targets_swapchain_locked(command_buffer) ||
            !find_bound_xfb_source_image_locked(command_buffer, &image, &info)) {
            return;
        }
        g_pending_xfb_captures[command_buffer] = PendingXfbCapture{image};
    }

    if (verbose_logging()) {
        std::fprintf(stderr,
                     "[frame-capture] queued XFB source after render pass %ux%u fmt=%d "
                     "layout=%d\n",
                     info.extent.width, info.extent.height, info.format, info.layout);
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
        g_next_set_debug_utils_object_name_ext = reinterpret_cast<PFN_vkSetDebugUtilsObjectNameEXT>(
            g_next_gipa(*instance, "vkSetDebugUtilsObjectNameEXT"));
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
        g_device = *device;
        if (g_get_memory_props) {
            g_get_memory_props(physical_device, &g_memory_props);
        }
        load_device_functions(*device);
        start_request_server_once();
        if (verbose_logging()) {
            std::fprintf(stderr, "[frame-capture] vkCreateDevice ok memoryTypes=%u\n",
                         g_memory_props.memoryTypeCount);
        }
    }
    return result;
}

extern "C" VKAPI_ATTR VkResult VKAPI_CALL vkCreateImage(VkDevice device,
                                                        const VkImageCreateInfo *create_info,
                                                        const VkAllocationCallbacks *allocator,
                                                        VkImage *image) {
    if (!g_next_create_image || !create_info) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkImageCreateInfo patched_create_info = *create_info;
    if ((patched_create_info.usage &
         (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT)) &&
        is_supported_format(patched_create_info.format)) {
        patched_create_info.usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    }

    VkResult result = g_next_create_image(device, &patched_create_info, allocator, image);
    if (result == VK_SUCCESS && image && *image != VK_NULL_HANDLE) {
        ImageInfo info{};
        info.format = patched_create_info.format;
        info.extent = patched_create_info.extent;
        info.usage = patched_create_info.usage;
        info.samples = patched_create_info.samples;
        info.layout = patched_create_info.initialLayout;
        std::lock_guard<std::mutex> lock(g_mu);
        g_images[*image] = info;
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
    if (!g_next_create_image_view || !create_info) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkResult result = g_next_create_image_view(device, create_info, allocator, view);
    if (result == VK_SUCCESS && view && *view != VK_NULL_HANDLE) {
        ImageViewInfo info{};
        info.image = create_info->image;
        info.format = create_info->format;
        info.aspect = create_info->subresourceRange.aspectMask;
        std::lock_guard<std::mutex> lock(g_mu);
        g_image_views[*view] = info;
    }
    return result;
}

extern "C" VKAPI_ATTR VkResult VKAPI_CALL
vkCreateSwapchainKHR(VkDevice device, const VkSwapchainCreateInfoKHR *create_info,
                     const VkAllocationCallbacks *allocator, VkSwapchainKHR *swapchain) {
    if (!g_next_create_swapchain_khr || !create_info) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkResult result = g_next_create_swapchain_khr(device, create_info, allocator, swapchain);
    if (result == VK_SUCCESS && swapchain && *swapchain != VK_NULL_HANDLE) {
        std::lock_guard<std::mutex> lock(g_mu);
        g_swapchains[*swapchain] =
            SwapchainInfo{create_info->imageFormat, create_info->imageExtent, {}};
        if (verbose_logging()) {
            std::fprintf(stderr, "[frame-capture] swapchain %ux%u fmt=%d usage=0x%x\n",
                         create_info->imageExtent.width, create_info->imageExtent.height,
                         create_info->imageFormat, create_info->imageUsage);
        }
    }
    return result;
}

extern "C" VKAPI_ATTR void VKAPI_CALL vkDestroySwapchainKHR(
    VkDevice device, VkSwapchainKHR swapchain, const VkAllocationCallbacks *allocator) {
    {
        std::lock_guard<std::mutex> lock(g_mu);
        g_swapchains.erase(swapchain);
    }
    g_next_destroy_swapchain_khr(device, swapchain, allocator);
}

extern "C" VKAPI_ATTR VkResult VKAPI_CALL vkGetSwapchainImagesKHR(VkDevice device,
                                                                  VkSwapchainKHR swapchain,
                                                                  uint32_t *count,
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
    if (!g_next_create_render_pass || !create_info) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkResult result = g_next_create_render_pass(device, create_info, allocator, render_pass);
    if (result == VK_SUCCESS && render_pass && *render_pass != VK_NULL_HANDLE) {
        RenderPassInfo info{};
        info.attachments.reserve(create_info->attachmentCount);
        for (uint32_t i = 0; i < create_info->attachmentCount; ++i) {
            const VkAttachmentDescription &attachment = create_info->pAttachments[i];
            info.attachments.push_back(AttachmentInfo{
                attachment.format,
                attachment.initialLayout,
                attachment.finalLayout,
            });
        }

        info.subpasses.reserve(create_info->subpassCount);
        for (uint32_t i = 0; i < create_info->subpassCount; ++i) {
            const VkSubpassDescription &subpass = create_info->pSubpasses[i];
            SubpassInfo subpass_info{};
            subpass_info.color_attachments.reserve(subpass.colorAttachmentCount);
            for (uint32_t j = 0; j < subpass.colorAttachmentCount; ++j) {
                const uint32_t attachment = subpass.pColorAttachments[j].attachment;
                if (attachment != VK_ATTACHMENT_UNUSED) {
                    subpass_info.color_attachments.push_back(attachment);
                }
            }
            if (subpass.pDepthStencilAttachment) {
                subpass_info.depth_stencil_attachment = subpass.pDepthStencilAttachment->attachment;
            }
            info.subpasses.push_back(std::move(subpass_info));
        }

        std::lock_guard<std::mutex> lock(g_mu);
        g_render_passes[*render_pass] = std::move(info);
        if (verbose_logging()) {
            std::fprintf(stderr, "[frame-capture] render pass %p attachments=%u subpasses=%u\n",
                         reinterpret_cast<void *>(*render_pass), create_info->attachmentCount,
                         create_info->subpassCount);
        }
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
    if (!g_next_create_framebuffer || !create_info) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkResult result = g_next_create_framebuffer(device, create_info, allocator, framebuffer);
    if (result == VK_SUCCESS && framebuffer && *framebuffer != VK_NULL_HANDLE) {
        FramebufferInfo info{};
        info.render_pass = create_info->renderPass;
        info.width = create_info->width;
        info.height = create_info->height;
        info.layers = create_info->layers;
        info.attachments.reserve(create_info->attachmentCount);
        {
            std::lock_guard<std::mutex> lock(g_mu);
            for (uint32_t i = 0; i < create_info->attachmentCount; ++i) {
                VkImage image = VK_NULL_HANDLE;
                auto view_it = g_image_views.find(create_info->pAttachments[i]);
                if (view_it != g_image_views.end()) {
                    image = view_it->second.image;
                }
                info.attachments.push_back(image);
            }
            g_framebuffers[*framebuffer] = std::move(info);
            if (verbose_logging()) {
                std::fprintf(stderr,
                             "[frame-capture] framebuffer %p render_pass=%p attachments=%u "
                             "size=%ux%u\n",
                             reinterpret_cast<void *>(*framebuffer),
                             reinterpret_cast<void *>(create_info->renderPass),
                             create_info->attachmentCount, create_info->width, create_info->height);
            }
        }
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

static void clear_command_buffer_tracking_locked(VkCommandBuffer command_buffer) {
    g_active_render_passes.erase(command_buffer);
    g_pending_inline_capture_command_buffers.erase(command_buffer);
    g_command_buffer_descriptors.erase(command_buffer);
    g_pending_xfb_captures.erase(command_buffer);
}

extern "C" VKAPI_ATTR VkResult VKAPI_CALL
vkBeginCommandBuffer(VkCommandBuffer command_buffer, const VkCommandBufferBeginInfo *begin_info) {
    {
        std::lock_guard<std::mutex> lock(g_mu);
        clear_command_buffer_tracking_locked(command_buffer);
    }
    return g_next_begin_command_buffer(command_buffer, begin_info);
}

extern "C" VKAPI_ATTR void VKAPI_CALL vkFreeCommandBuffers(VkDevice device,
                                                           VkCommandPool command_pool,
                                                           uint32_t command_buffer_count,
                                                           const VkCommandBuffer *command_buffers) {
    if (command_buffers) {
        std::lock_guard<std::mutex> lock(g_mu);
        for (uint32_t i = 0; i < command_buffer_count; ++i) {
            clear_command_buffer_tracking_locked(command_buffers[i]);
        }
    }
    g_next_free_command_buffers(device, command_pool, command_buffer_count, command_buffers);
}

extern "C" VKAPI_ATTR void VKAPI_CALL vkCmdBeginRenderPass(VkCommandBuffer command_buffer,
                                                           const VkRenderPassBeginInfo *begin_info,
                                                           VkSubpassContents contents) {
    g_next_cmd_begin_render_pass(command_buffer, begin_info, contents);
    if (!begin_info) {
        return;
    }

    std::lock_guard<std::mutex> lock(g_mu);
    g_active_render_passes[command_buffer] =
        CommandBufferRenderPassInfo{begin_info->renderPass, begin_info->framebuffer, 0};

    auto framebuffer_it = g_framebuffers.find(begin_info->framebuffer);
    auto render_pass_it = g_render_passes.find(begin_info->renderPass);
    if (framebuffer_it == g_framebuffers.end() || render_pass_it == g_render_passes.end() ||
        render_pass_it->second.subpasses.empty()) {
        if (verbose_logging()) {
            std::fprintf(stderr, "[frame-capture] begin render pass missing metadata rp=%p fb=%p\n",
                         reinterpret_cast<void *>(begin_info->renderPass),
                         reinterpret_cast<void *>(begin_info->framebuffer));
        }
        return;
    }

    const RenderPassInfo &render_pass = render_pass_it->second;
    const FramebufferInfo &framebuffer = framebuffer_it->second;
    const SubpassInfo &subpass = render_pass.subpasses[0];
    for (uint32_t attachment_index : subpass.color_attachments) {
        if (attachment_index >= framebuffer.attachments.size()) {
            continue;
        }
        auto image_it = g_images.find(framebuffer.attachments[attachment_index]);
        if (image_it != g_images.end() && attachment_index < render_pass.attachments.size()) {
            image_it->second.layout = render_pass.attachments[attachment_index].initial_layout;
        }
    }
    const uint32_t depth_index = subpass.depth_stencil_attachment;
    if (depth_index != VK_ATTACHMENT_UNUSED && depth_index < framebuffer.attachments.size()) {
        auto image_it = g_images.find(framebuffer.attachments[depth_index]);
        if (image_it != g_images.end() && depth_index < render_pass.attachments.size()) {
            image_it->second.layout = render_pass.attachments[depth_index].initial_layout;
        }
    }
}

extern "C" VKAPI_ATTR void VKAPI_CALL vkCmdEndRenderPass(VkCommandBuffer command_buffer) {
    CommandBufferRenderPassInfo active{};
    bool has_active = false;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        auto active_it = g_active_render_passes.find(command_buffer);
        if (active_it != g_active_render_passes.end()) {
            active = active_it->second;
            has_active = true;
        }
    }

    g_next_cmd_end_render_pass(command_buffer);

    if (!has_active) {
        return;
    }

    VkImage xfb_capture_image = VK_NULL_HANDLE;
    ImageInfo xfb_capture_info{};
    {
        std::lock_guard<std::mutex> lock(g_mu);
        auto active_it = g_active_render_passes.find(command_buffer);
        if (active_it != g_active_render_passes.end()) {
            g_active_render_passes.erase(active_it);
        }

        auto framebuffer_it = g_framebuffers.find(active.framebuffer);
        auto render_pass_it = g_render_passes.find(active.render_pass);
        if (framebuffer_it == g_framebuffers.end() || render_pass_it == g_render_passes.end()) {
            if (verbose_logging()) {
                std::fprintf(stderr,
                             "[frame-capture] end render pass missing metadata rp=%p fb=%p\n",
                             reinterpret_cast<void *>(active.render_pass),
                             reinterpret_cast<void *>(active.framebuffer));
            }
            return;
        }
        const RenderPassInfo &render_pass = render_pass_it->second;
        const FramebufferInfo &framebuffer = framebuffer_it->second;
        if (active.subpass >= render_pass.subpasses.size()) {
            return;
        }

        const SubpassInfo &subpass = render_pass.subpasses[active.subpass];
        for (uint32_t attachment_index : subpass.color_attachments) {
            if (attachment_index >= framebuffer.attachments.size()) {
                continue;
            }
            auto image_it = g_images.find(framebuffer.attachments[attachment_index]);
            if (image_it == g_images.end()) {
                continue;
            }
            if (attachment_index < render_pass.attachments.size()) {
                image_it->second.layout = render_pass.attachments[attachment_index].final_layout;
            }
        }

        const uint32_t depth_index = subpass.depth_stencil_attachment;
        if (depth_index != VK_ATTACHMENT_UNUSED && depth_index < framebuffer.attachments.size()) {
            auto image_it = g_images.find(framebuffer.attachments[depth_index]);
            if (image_it != g_images.end() && depth_index < render_pass.attachments.size()) {
                image_it->second.layout = render_pass.attachments[depth_index].final_layout;
            }
        }

        auto pending_xfb_it = g_pending_xfb_captures.find(command_buffer);
        if (pending_xfb_it != g_pending_xfb_captures.end()) {
            auto image_it = g_images.find(pending_xfb_it->second.image);
            if (image_it != g_images.end()) {
                xfb_capture_image = image_it->first;
                xfb_capture_info = image_it->second;
            }
            g_pending_xfb_captures.erase(pending_xfb_it);
        }
    }

    if (xfb_capture_image != VK_NULL_HANDLE) {
        record_inline_capture_to_latest(command_buffer, xfb_capture_image, xfb_capture_info,
                                        "xfb-source");
    }
}

extern "C" VKAPI_ATTR void VKAPI_CALL vkCmdPipelineBarrier(
    VkCommandBuffer command_buffer, VkPipelineStageFlags src_stage_mask,
    VkPipelineStageFlags dst_stage_mask, VkDependencyFlags dependency_flags,
    uint32_t memory_barrier_count, const VkMemoryBarrier *memory_barriers,
    uint32_t buffer_memory_barrier_count, const VkBufferMemoryBarrier *buffer_memory_barriers,
    uint32_t image_memory_barrier_count, const VkImageMemoryBarrier *image_memory_barriers) {
    g_next_cmd_pipeline_barrier(command_buffer, src_stage_mask, dst_stage_mask, dependency_flags,
                                memory_barrier_count, memory_barriers, buffer_memory_barrier_count,
                                buffer_memory_barriers, image_memory_barrier_count,
                                image_memory_barriers);

    if (!image_memory_barriers || image_memory_barrier_count == 0) {
        return;
    }

    std::lock_guard<std::mutex> lock(g_mu);
    for (uint32_t i = 0; i < image_memory_barrier_count; ++i) {
        const VkImageMemoryBarrier &barrier = image_memory_barriers[i];
        auto it = g_images.find(barrier.image);
        if (it != g_images.end()) {
            it->second.layout = barrier.newLayout;
        }
    }
}

extern "C" VKAPI_ATTR VkResult VKAPI_CALL
vkSetDebugUtilsObjectNameEXT(VkDevice device, const VkDebugUtilsObjectNameInfoEXT *name_info) {
    if (name_info && name_info->objectType == VK_OBJECT_TYPE_IMAGE) {
        std::lock_guard<std::mutex> lock(g_mu);
        auto it = g_images.find(reinterpret_cast<VkImage>(name_info->objectHandle));
        if (it != g_images.end()) {
            it->second.name = name_info->pObjectName ? name_info->pObjectName : "";
            if (verbose_logging()) {
                std::fprintf(stderr, "[frame-capture] image name '%s' %ux%u fmt=%d usage=0x%x\n",
                             it->second.name.c_str(), it->second.extent.width,
                             it->second.extent.height, it->second.format, it->second.usage);
            }
        }
    }
    if (g_next_set_debug_utils_object_name_ext) {
        return g_next_set_debug_utils_object_name_ext(device, name_info);
    }
    return VK_SUCCESS;
}

static void append_unique_image(std::vector<VkImage> *images, VkImage image) {
    if (image == VK_NULL_HANDLE) {
        return;
    }
    for (VkImage existing : *images) {
        if (existing == image) {
            return;
        }
    }
    images->push_back(image);
}

extern "C" VKAPI_ATTR void VKAPI_CALL vkUpdateDescriptorSets(
    VkDevice device, uint32_t descriptor_write_count, const VkWriteDescriptorSet *descriptor_writes,
    uint32_t descriptor_copy_count, const VkCopyDescriptorSet *descriptor_copies) {
    g_next_update_descriptor_sets(device, descriptor_write_count, descriptor_writes,
                                  descriptor_copy_count, descriptor_copies);

    if (!descriptor_writes) {
        return;
    }

    std::lock_guard<std::mutex> lock(g_mu);
    for (uint32_t i = 0; i < descriptor_write_count; ++i) {
        const VkWriteDescriptorSet &write = descriptor_writes[i];
        if ((write.descriptorType != VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER &&
             write.descriptorType != VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE) ||
            !write.pImageInfo) {
            continue;
        }

        DescriptorSetInfo &set_info = g_descriptor_sets[write.dstSet];
        for (uint32_t j = 0; j < write.descriptorCount; ++j) {
            auto view_it = g_image_views.find(write.pImageInfo[j].imageView);
            if (view_it != g_image_views.end()) {
                append_unique_image(&set_info.sampled_images, view_it->second.image);
                set_info.sampled_images_by_binding[write.dstBinding + write.dstArrayElement + j] =
                    view_it->second.image;
            }
        }
    }
}

extern "C" VKAPI_ATTR void VKAPI_CALL
vkCmdBindDescriptorSets(VkCommandBuffer command_buffer, VkPipelineBindPoint pipeline_bind_point,
                        VkPipelineLayout layout, uint32_t first_set, uint32_t descriptor_set_count,
                        const VkDescriptorSet *descriptor_sets, uint32_t dynamic_offset_count,
                        const uint32_t *dynamic_offsets) {
    g_next_cmd_bind_descriptor_sets(command_buffer, pipeline_bind_point, layout, first_set,
                                    descriptor_set_count, descriptor_sets, dynamic_offset_count,
                                    dynamic_offsets);

    if (pipeline_bind_point != VK_PIPELINE_BIND_POINT_GRAPHICS || !descriptor_sets) {
        return;
    }

    std::lock_guard<std::mutex> lock(g_mu);
    CommandBufferDescriptorInfo &command_info = g_command_buffer_descriptors[command_buffer];
    command_info.sampled_images.clear();
    command_info.sampled_images_by_binding.clear();
    for (uint32_t i = 0; i < descriptor_set_count; ++i) {
        auto set_it = g_descriptor_sets.find(descriptor_sets[i]);
        if (set_it == g_descriptor_sets.end()) {
            continue;
        }
        for (VkImage image : set_it->second.sampled_images) {
            append_unique_image(&command_info.sampled_images, image);
        }
        for (const auto &binding : set_it->second.sampled_images_by_binding) {
            command_info.sampled_images_by_binding[binding.first] = binding.second;
        }
    }
}

extern "C" VKAPI_ATTR void VKAPI_CALL vkCmdDraw(VkCommandBuffer command_buffer,
                                                uint32_t vertex_count, uint32_t instance_count,
                                                uint32_t first_vertex, uint32_t first_instance) {
    remember_bound_xfb_source(command_buffer);
    g_next_cmd_draw(command_buffer, vertex_count, instance_count, first_vertex, first_instance);
}

extern "C" VKAPI_ATTR void VKAPI_CALL vkCmdDrawIndexed(VkCommandBuffer command_buffer,
                                                       uint32_t index_count,
                                                       uint32_t instance_count,
                                                       uint32_t first_index, int32_t vertex_offset,
                                                       uint32_t first_instance) {
    remember_bound_xfb_source(command_buffer);
    g_next_cmd_draw_indexed(command_buffer, index_count, instance_count, first_index, vertex_offset,
                            first_instance);
}

extern "C" VKAPI_ATTR VkResult VKAPI_CALL vkQueueSubmit(VkQueue queue, uint32_t submit_count,
                                                        const VkSubmitInfo *submits,
                                                        VkFence fence) {
    bool has_inline_capture = false;
    if (submits) {
        std::lock_guard<std::mutex> lock(g_mu);
        for (uint32_t i = 0; i < submit_count; ++i) {
            const VkSubmitInfo &submit = submits[i];
            for (uint32_t j = 0; j < submit.commandBufferCount; ++j) {
                if (g_pending_inline_capture_command_buffers.contains(submit.pCommandBuffers[j])) {
                    has_inline_capture = true;
                }
            }
        }
    }

    VkResult result = g_next_queue_submit(queue, submit_count, submits, fence);
    if (result == VK_SUCCESS && has_inline_capture && g_next_queue_wait_idle) {
        VkResult wait_result = g_next_queue_wait_idle(queue);
        if (wait_result != VK_SUCCESS) {
            log_capture_failure("vkQueueWaitIdle(inline capture) failed", wait_result);
        } else if (verbose_logging()) {
            std::fprintf(stderr, "[frame-capture] inline capture submitted and idle\n");
        }
        std::lock_guard<std::mutex> lock(g_mu);
        for (uint32_t i = 0; i < submit_count; ++i) {
            const VkSubmitInfo &submit = submits[i];
            for (uint32_t j = 0; j < submit.commandBufferCount; ++j) {
                g_pending_inline_capture_command_buffers.erase(submit.pCommandBuffers[j]);
            }
        }
    }
    return result;
}

extern "C" VKAPI_ATTR VkResult VKAPI_CALL vkQueuePresentKHR(VkQueue queue,
                                                            const VkPresentInfoKHR *present_info) {
    if (!g_next_queue_present_khr || !present_info) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    return g_next_queue_present_khr(queue, present_info);
}

extern "C" VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice device,
                                                                        const char *name) {
    if (std::strcmp(name, "vkGetDeviceProcAddr") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(vkGetDeviceProcAddr);
    }
    if (std::strcmp(name, "vkQueuePresentKHR") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(vkQueuePresentKHR);
    }
    if (std::strcmp(name, "vkQueueSubmit") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(vkQueueSubmit);
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
    if (std::strcmp(name, "vkCreateFramebuffer") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(vkCreateFramebuffer);
    }
    if (std::strcmp(name, "vkDestroyFramebuffer") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(vkDestroyFramebuffer);
    }
    if (std::strcmp(name, "vkCreateRenderPass") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(vkCreateRenderPass);
    }
    if (std::strcmp(name, "vkDestroyRenderPass") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(vkDestroyRenderPass);
    }
    if (std::strcmp(name, "vkBeginCommandBuffer") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(vkBeginCommandBuffer);
    }
    if (std::strcmp(name, "vkFreeCommandBuffers") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(vkFreeCommandBuffers);
    }
    if (std::strcmp(name, "vkCmdBeginRenderPass") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(vkCmdBeginRenderPass);
    }
    if (std::strcmp(name, "vkCmdEndRenderPass") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(vkCmdEndRenderPass);
    }
    if (std::strcmp(name, "vkUpdateDescriptorSets") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(vkUpdateDescriptorSets);
    }
    if (std::strcmp(name, "vkCmdBindDescriptorSets") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(vkCmdBindDescriptorSets);
    }
    if (std::strcmp(name, "vkCmdDraw") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(vkCmdDraw);
    }
    if (std::strcmp(name, "vkCmdDrawIndexed") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(vkCmdDrawIndexed);
    }
    if (std::strcmp(name, "vkCmdPipelineBarrier") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(vkCmdPipelineBarrier);
    }
    if (std::strcmp(name, "vkSetDebugUtilsObjectNameEXT") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(vkSetDebugUtilsObjectNameEXT);
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
    if (std::strcmp(name, "vkQueuePresentKHR") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(vkQueuePresentKHR);
    }
    if (std::strcmp(name, "vkQueueSubmit") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(vkQueueSubmit);
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
    if (std::strcmp(name, "vkCreateFramebuffer") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(vkCreateFramebuffer);
    }
    if (std::strcmp(name, "vkDestroyFramebuffer") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(vkDestroyFramebuffer);
    }
    if (std::strcmp(name, "vkCreateRenderPass") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(vkCreateRenderPass);
    }
    if (std::strcmp(name, "vkDestroyRenderPass") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(vkDestroyRenderPass);
    }
    if (std::strcmp(name, "vkBeginCommandBuffer") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(vkBeginCommandBuffer);
    }
    if (std::strcmp(name, "vkFreeCommandBuffers") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(vkFreeCommandBuffers);
    }
    if (std::strcmp(name, "vkCmdBeginRenderPass") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(vkCmdBeginRenderPass);
    }
    if (std::strcmp(name, "vkCmdEndRenderPass") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(vkCmdEndRenderPass);
    }
    if (std::strcmp(name, "vkUpdateDescriptorSets") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(vkUpdateDescriptorSets);
    }
    if (std::strcmp(name, "vkCmdBindDescriptorSets") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(vkCmdBindDescriptorSets);
    }
    if (std::strcmp(name, "vkCmdDraw") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(vkCmdDraw);
    }
    if (std::strcmp(name, "vkCmdDrawIndexed") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(vkCmdDrawIndexed);
    }
    if (std::strcmp(name, "vkCmdPipelineBarrier") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(vkCmdPipelineBarrier);
    }
    if (std::strcmp(name, "vkSetDebugUtilsObjectNameEXT") == 0) {
        return reinterpret_cast<PFN_vkVoidFunction>(vkSetDebugUtilsObjectNameEXT);
    }
    if (g_next_gipa) {
        return g_next_gipa(instance, name);
    }
    return nullptr;
}