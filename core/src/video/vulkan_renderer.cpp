#include "vulkan_renderer.hpp"

#include <algorithm>
#include <array>
#include <cstring>

#include "utils/log.hpp"

// Platform surface headers
#ifdef __linux__
#include <X11/Xlib.h>
#include <vulkan/vulkan_xlib.h>
#elif defined(_WIN32)
#include <windows.h>
#include <vulkan/vulkan_win32.h>
#endif

// Embedded SPIR-V (generated at build time)
#include "quad_vert.h"
#include "quad_frag.h"

// ---- debug callback (debug builds only) ----
#ifndef NDEBUG
static VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT /*type*/,
    const VkDebugUtilsMessengerCallbackDataEXT* data,
    void* /*user*/)
{
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        LOG_ERROR() << "Vulkan: " << data->pMessage;
    } else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        LOG_WARNING() << "Vulkan: " << data->pMessage;
    }
    return VK_FALSE;
}
#endif

// ---- is_available ----
bool VulkanRenderer::is_available()
{
    VkApplicationInfo app {};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.apiVersion = VK_API_VERSION_1_0;

    VkInstanceCreateInfo ci {};
    ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo = &app;

    VkInstance inst = VK_NULL_HANDLE;
    if (vkCreateInstance(&ci, nullptr, &inst) != VK_SUCCESS)
        return false;

    uint32_t count = 0;
    vkEnumeratePhysicalDevices(inst, &count, nullptr);
    vkDestroyInstance(inst, nullptr);
    return count > 0;
}

// ---- init ----
bool VulkanRenderer::init(uint64_t native_handle, uint64_t display_handle, int w, int h)
{
    if (!create_instance()) return false;
    if (!create_surface(native_handle, display_handle)) return false;
    if (!pick_physical_device()) return false;
    if (!create_device()) return false;
    if (!create_command_pool()) return false;
    if (!create_swapchain(w, h)) return false;
    if (!create_render_pass()) return false;
    if (!create_sampler()) return false;
    if (!create_descriptor_layout()) return false;
    if (!create_descriptor_pool_and_set()) return false;
    if (!create_pipeline()) return false;
    if (!create_framebuffers()) return false;
    if (!create_sync_objects()) return false;

    // Allocate single command buffer
    VkCommandBufferAllocateInfo ai {};
    ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool = cmd_pool_;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    vkAllocateCommandBuffers(device_, &ai, &cmd_buf_);

    LOG_INFO() << "VulkanRenderer: initialized on "
               << swapchain_extent_.width << "x" << swapchain_extent_.height;
    return true;
}

// ---- destroy ----
void VulkanRenderer::destroy()
{
    if (!device_) return;
    vkDeviceWaitIdle(device_);

    for (auto& t : textures_) t.destroy();
    cleanup_swapchain();

    if (sampler_) vkDestroySampler(device_, sampler_, nullptr);
    if (desc_pool_) vkDestroyDescriptorPool(device_, desc_pool_, nullptr);
    if (desc_set_layout_) vkDestroyDescriptorSetLayout(device_, desc_set_layout_, nullptr);
    if (pipeline_) vkDestroyPipeline(device_, pipeline_, nullptr);
    if (pipeline_layout_) vkDestroyPipelineLayout(device_, pipeline_layout_, nullptr);
    if (render_pass_) vkDestroyRenderPass(device_, render_pass_, nullptr);
    if (sem_image_available_) vkDestroySemaphore(device_, sem_image_available_, nullptr);
    if (sem_render_finished_) vkDestroySemaphore(device_, sem_render_finished_, nullptr);
    if (fence_in_flight_) vkDestroyFence(device_, fence_in_flight_, nullptr);
    if (cmd_pool_) vkDestroyCommandPool(device_, cmd_pool_, nullptr);
    if (device_) vkDestroyDevice(device_, nullptr);
    if (surface_) vkDestroySurfaceKHR(instance_, surface_, nullptr);

#ifndef NDEBUG
    if (debug_messenger_) {
        auto fn = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance_, "vkDestroyDebugUtilsMessengerEXT"));
        if (fn) fn(instance_, debug_messenger_, nullptr);
    }
#endif

    if (instance_) vkDestroyInstance(instance_, nullptr);

    instance_ = VK_NULL_HANDLE;
    device_ = VK_NULL_HANDLE;
    LOG_INFO() << "VulkanRenderer: destroyed";
}

// ---- acquire_staging ----
uint8_t* VulkanRenderer::acquire_staging(int w, int h, size_t& capacity)
{
    auto& tex = textures_[current_tex_];

    // (Re)create texture if dimensions changed
    if (tex.width() != w || tex.height() != h) {
        vkDeviceWaitIdle(device_);
        tex.destroy();
        if (!tex.init(device_, phys_device_, cmd_pool_, graphics_queue_, w, h)) {
            capacity = 0;
            return nullptr;
        }
    }

    capacity = tex.capacity();
    return tex.mapped_ptr();
}

// ---- present ----
void VulkanRenderer::present(int frame_w, int frame_h)
{
    std::scoped_lock lk(resize_mutex_);

    vkWaitForFences(device_, 1, &fence_in_flight_, VK_TRUE, UINT64_MAX);

    uint32_t img_idx;
    VkResult result = vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX,
        sem_image_available_, VK_NULL_HANDLE, &img_idx);

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        // Swapchain invalidated, skip this frame; resize() will fix it
        return;
    }

    vkResetFences(device_, 1, &fence_in_flight_);

    auto& tex = textures_[current_tex_];
    update_descriptor_set(tex.image_view());

    // Record command buffer
    vkResetCommandBuffer(cmd_buf_, 0);

    VkCommandBufferBeginInfo begin {};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd_buf_, &begin);

    // Flush staging -> VkImage
    tex.flush_to_image(cmd_buf_);

    // Begin render pass
    VkClearValue clear {};
    clear.color = {{ 0.0f, 0.0f, 0.0f, 1.0f }};

    VkRenderPassBeginInfo rp {};
    rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp.renderPass = render_pass_;
    rp.framebuffer = framebuffers_[img_idx];
    rp.renderArea.extent = swapchain_extent_;
    rp.clearValueCount = 1;
    rp.pClearValues = &clear;
    vkCmdBeginRenderPass(cmd_buf_, &rp, VK_SUBPASS_CONTENTS_INLINE);

    vkCmdBindPipeline(cmd_buf_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    vkCmdBindDescriptorSets(cmd_buf_, VK_PIPELINE_BIND_POINT_GRAPHICS,
        pipeline_layout_, 0, 1, &desc_set_, 0, nullptr);

    // Push constants: letterbox scale + offset
    float pc[4];
    compute_letterbox(frame_w, frame_h, pc);
    vkCmdPushConstants(cmd_buf_, pipeline_layout_, VK_SHADER_STAGE_FRAGMENT_BIT,
        0, sizeof(pc), pc);

    // Viewport + scissor
    VkViewport vp {};
    vp.width = static_cast<float>(swapchain_extent_.width);
    vp.height = static_cast<float>(swapchain_extent_.height);
    vp.maxDepth = 1.0f;
    vkCmdSetViewport(cmd_buf_, 0, 1, &vp);

    VkRect2D scissor {};
    scissor.extent = swapchain_extent_;
    vkCmdSetScissor(cmd_buf_, 0, 1, &scissor);

    vkCmdDraw(cmd_buf_, 3, 1, 0, 0); // fullscreen triangle

    vkCmdEndRenderPass(cmd_buf_);
    vkEndCommandBuffer(cmd_buf_);

    // Submit
    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit {};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.waitSemaphoreCount = 1;
    submit.pWaitSemaphores = &sem_image_available_;
    submit.pWaitDstStageMask = &wait_stage;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd_buf_;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &sem_render_finished_;
    vkQueueSubmit(graphics_queue_, 1, &submit, fence_in_flight_);

    // Present
    VkPresentInfoKHR pi {};
    pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &sem_render_finished_;
    pi.swapchainCount = 1;
    pi.pSwapchains = &swapchain_;
    pi.pImageIndices = &img_idx;
    result = vkQueuePresentKHR(graphics_queue_, &pi);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        // Will be handled by next resize() call
    }

    current_tex_ = 1 - current_tex_;
}

// ---- resize ----
void VulkanRenderer::resize(int w, int h)
{
    if (w <= 0 || h <= 0) return;

    std::scoped_lock lk(resize_mutex_);
    vkDeviceWaitIdle(device_);
    cleanup_swapchain();
    create_swapchain(w, h);
    create_framebuffers();
}

// ---- create_instance ----
bool VulkanRenderer::create_instance()
{
    VkApplicationInfo app {};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "Driscord";
    app.applicationVersion = VK_MAKE_VERSION(0, 3, 0);
    app.apiVersion = VK_API_VERSION_1_0;

    std::vector<const char*> extensions = {
        VK_KHR_SURFACE_EXTENSION_NAME,
#ifdef __linux__
        VK_KHR_XLIB_SURFACE_EXTENSION_NAME,
#elif defined(_WIN32)
        VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
#endif
    };

    std::vector<const char*> layers;
#ifndef NDEBUG
    layers.push_back("VK_LAYER_KHRONOS_validation");
    extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
#endif

    VkInstanceCreateInfo ci {};
    ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo = &app;
    ci.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    ci.ppEnabledExtensionNames = extensions.data();
    ci.enabledLayerCount = static_cast<uint32_t>(layers.size());
    ci.ppEnabledLayerNames = layers.data();

    if (vkCreateInstance(&ci, nullptr, &instance_) != VK_SUCCESS) {
        LOG_ERROR() << "VulkanRenderer: failed to create instance";
        return false;
    }

#ifndef NDEBUG
    auto fn = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance_, "vkCreateDebugUtilsMessengerEXT"));
    if (fn) {
        VkDebugUtilsMessengerCreateInfoEXT dci {};
        dci.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        dci.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT
            | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        dci.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT
            | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
            | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        dci.pfnUserCallback = debug_callback;
        fn(instance_, &dci, nullptr, &debug_messenger_);
    }
#endif

    return true;
}

// ---- create_surface ----
bool VulkanRenderer::create_surface(uint64_t native_handle, uint64_t display_handle)
{
#ifdef __linux__
    VkXlibSurfaceCreateInfoKHR si {};
    si.sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR;
    si.dpy = reinterpret_cast<Display*>(display_handle);
    si.window = static_cast<Window>(native_handle);
    if (vkCreateXlibSurfaceKHR(instance_, &si, nullptr, &surface_) != VK_SUCCESS) {
        LOG_ERROR() << "VulkanRenderer: failed to create Xlib surface";
        return false;
    }
#elif defined(_WIN32)
    (void)display_handle;
    VkWin32SurfaceCreateInfoKHR si {};
    si.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    si.hwnd = reinterpret_cast<HWND>(native_handle);
    si.hinstance = GetModuleHandle(nullptr);
    if (vkCreateWin32SurfaceKHR(instance_, &si, nullptr, &surface_) != VK_SUCCESS) {
        LOG_ERROR() << "VulkanRenderer: failed to create Win32 surface";
        return false;
    }
#else
    (void)native_handle; (void)display_handle;
    LOG_ERROR() << "VulkanRenderer: unsupported platform";
    return false;
#endif
    return true;
}

// ---- pick_physical_device ----
bool VulkanRenderer::pick_physical_device()
{
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(instance_, &count, nullptr);
    if (count == 0) {
        LOG_ERROR() << "VulkanRenderer: no Vulkan devices found";
        return false;
    }

    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(instance_, &count, devices.data());

    // Prefer discrete GPU
    for (auto d : devices) {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(d, &props);

        // Check queue family supports graphics + present
        uint32_t qf_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(d, &qf_count, nullptr);
        std::vector<VkQueueFamilyProperties> qf_props(qf_count);
        vkGetPhysicalDeviceQueueFamilyProperties(d, &qf_count, qf_props.data());

        for (uint32_t i = 0; i < qf_count; ++i) {
            if (!(qf_props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) continue;
            VkBool32 present_support = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(d, i, surface_, &present_support);
            if (!present_support) continue;

            if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU || phys_device_ == VK_NULL_HANDLE) {
                phys_device_ = d;
                queue_family_ = i;
                LOG_INFO() << "VulkanRenderer: using " << props.deviceName;
                if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
                    return true;
            }
        }
    }

    return phys_device_ != VK_NULL_HANDLE;
}

// ---- create_device ----
bool VulkanRenderer::create_device()
{
    float priority = 1.0f;
    VkDeviceQueueCreateInfo qci {};
    qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qci.queueFamilyIndex = queue_family_;
    qci.queueCount = 1;
    qci.pQueuePriorities = &priority;

    const char* ext = VK_KHR_SWAPCHAIN_EXTENSION_NAME;

    VkDeviceCreateInfo dci {};
    dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    dci.enabledExtensionCount = 1;
    dci.ppEnabledExtensionNames = &ext;

    if (vkCreateDevice(phys_device_, &dci, nullptr, &device_) != VK_SUCCESS) {
        LOG_ERROR() << "VulkanRenderer: failed to create logical device";
        return false;
    }

    vkGetDeviceQueue(device_, queue_family_, 0, &graphics_queue_);
    return true;
}

// ---- create_swapchain ----
bool VulkanRenderer::create_swapchain(int w, int h)
{
    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(phys_device_, surface_, &caps);

    // Choose extent
    if (caps.currentExtent.width != UINT32_MAX) {
        swapchain_extent_ = caps.currentExtent;
    } else {
        swapchain_extent_.width = std::clamp(static_cast<uint32_t>(w),
            caps.minImageExtent.width, caps.maxImageExtent.width);
        swapchain_extent_.height = std::clamp(static_cast<uint32_t>(h),
            caps.minImageExtent.height, caps.maxImageExtent.height);
    }

    // Choose format
    uint32_t fmt_count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(phys_device_, surface_, &fmt_count, nullptr);
    std::vector<VkSurfaceFormatKHR> fmts(fmt_count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(phys_device_, surface_, &fmt_count, fmts.data());

    swapchain_format_ = fmts[0].format;
    VkColorSpaceKHR color_space = fmts[0].colorSpace;
    for (auto& f : fmts) {
        if (f.format == VK_FORMAT_B8G8R8A8_SRGB
            && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            swapchain_format_ = f.format;
            color_space = f.colorSpace;
            break;
        }
    }

    uint32_t img_count = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && img_count > caps.maxImageCount)
        img_count = caps.maxImageCount;

    VkSwapchainCreateInfoKHR sci {};
    sci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    sci.surface = surface_;
    sci.minImageCount = img_count;
    sci.imageFormat = swapchain_format_;
    sci.imageColorSpace = color_space;
    sci.imageExtent = swapchain_extent_;
    sci.imageArrayLayers = 1;
    sci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    sci.preTransform = caps.currentTransform;
    sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    sci.presentMode = VK_PRESENT_MODE_FIFO_KHR; // vsync, always available
    sci.clipped = VK_TRUE;

    if (vkCreateSwapchainKHR(device_, &sci, nullptr, &swapchain_) != VK_SUCCESS) {
        LOG_ERROR() << "VulkanRenderer: failed to create swapchain";
        return false;
    }

    // Retrieve swapchain images
    vkGetSwapchainImagesKHR(device_, swapchain_, &img_count, nullptr);
    swapchain_images_.resize(img_count);
    vkGetSwapchainImagesKHR(device_, swapchain_, &img_count, swapchain_images_.data());

    // Create image views
    swapchain_views_.resize(img_count);
    for (uint32_t i = 0; i < img_count; ++i) {
        VkImageViewCreateInfo vi {};
        vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image = swapchain_images_[i];
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = swapchain_format_;
        vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        vi.subresourceRange.levelCount = 1;
        vi.subresourceRange.layerCount = 1;
        vkCreateImageView(device_, &vi, nullptr, &swapchain_views_[i]);
    }

    return true;
}

// ---- create_render_pass ----
bool VulkanRenderer::create_render_pass()
{
    VkAttachmentDescription att {};
    att.format = swapchain_format_;
    att.samples = VK_SAMPLE_COUNT_1_BIT;
    att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    att.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference ref {};
    ref.attachment = 0;
    ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription sub {};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments = &ref;

    VkSubpassDependency dep {};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo rpi {};
    rpi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpi.attachmentCount = 1;
    rpi.pAttachments = &att;
    rpi.subpassCount = 1;
    rpi.pSubpasses = &sub;
    rpi.dependencyCount = 1;
    rpi.pDependencies = &dep;

    return vkCreateRenderPass(device_, &rpi, nullptr, &render_pass_) == VK_SUCCESS;
}

// ---- create_descriptor_layout ----
bool VulkanRenderer::create_descriptor_layout()
{
    VkDescriptorSetLayoutBinding binding {};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo ci {};
    ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    ci.bindingCount = 1;
    ci.pBindings = &binding;

    return vkCreateDescriptorSetLayout(device_, &ci, nullptr, &desc_set_layout_) == VK_SUCCESS;
}

// ---- create_pipeline ----
bool VulkanRenderer::create_pipeline()
{
    // Shader modules from embedded SPIR-V
    auto create_module = [&](const uint32_t* code, size_t size) -> VkShaderModule {
        VkShaderModuleCreateInfo ci {};
        ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        ci.codeSize = size;
        ci.pCode = code;
        VkShaderModule mod = VK_NULL_HANDLE;
        vkCreateShaderModule(device_, &ci, nullptr, &mod);
        return mod;
    };

    VkShaderModule vert = create_module(
        reinterpret_cast<const uint32_t*>(quad_vert_spv), sizeof(quad_vert_spv));
    VkShaderModule frag = create_module(
        reinterpret_cast<const uint32_t*>(quad_frag_spv), sizeof(quad_frag_spv));

    VkPipelineShaderStageCreateInfo stages[2] {};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag;
    stages[1].pName = "main";

    // No vertex input (fullscreen triangle generated in shader)
    VkPipelineVertexInputStateCreateInfo vertex_input {};
    vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo input_asm {};
    input_asm.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_asm.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo vp_state {};
    vp_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp_state.viewportCount = 1;
    vp_state.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo raster {};
    raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.lineWidth = 1.0f;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo ms {};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState blend_att {};
    blend_att.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
        | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo blend {};
    blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend.attachmentCount = 1;
    blend.pAttachments = &blend_att;

    VkDynamicState dyn_states[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dyn {};
    dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dyn.dynamicStateCount = 2;
    dyn.pDynamicStates = dyn_states;

    // Push constant for letterbox scale/offset (4 floats = 16 bytes)
    VkPushConstantRange pc_range {};
    pc_range.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pc_range.offset = 0;
    pc_range.size = 4 * sizeof(float);

    VkPipelineLayoutCreateInfo layout_ci {};
    layout_ci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout_ci.setLayoutCount = 1;
    layout_ci.pSetLayouts = &desc_set_layout_;
    layout_ci.pushConstantRangeCount = 1;
    layout_ci.pPushConstantRanges = &pc_range;
    vkCreatePipelineLayout(device_, &layout_ci, nullptr, &pipeline_layout_);

    VkGraphicsPipelineCreateInfo pi {};
    pi.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pi.stageCount = 2;
    pi.pStages = stages;
    pi.pVertexInputState = &vertex_input;
    pi.pInputAssemblyState = &input_asm;
    pi.pViewportState = &vp_state;
    pi.pRasterizationState = &raster;
    pi.pMultisampleState = &ms;
    pi.pColorBlendState = &blend;
    pi.pDynamicState = &dyn;
    pi.layout = pipeline_layout_;
    pi.renderPass = render_pass_;

    VkResult res = vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pi, nullptr, &pipeline_);

    vkDestroyShaderModule(device_, vert, nullptr);
    vkDestroyShaderModule(device_, frag, nullptr);

    return res == VK_SUCCESS;
}

// ---- create_framebuffers ----
bool VulkanRenderer::create_framebuffers()
{
    framebuffers_.resize(swapchain_views_.size());
    for (size_t i = 0; i < swapchain_views_.size(); ++i) {
        VkFramebufferCreateInfo fi {};
        fi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fi.renderPass = render_pass_;
        fi.attachmentCount = 1;
        fi.pAttachments = &swapchain_views_[i];
        fi.width = swapchain_extent_.width;
        fi.height = swapchain_extent_.height;
        fi.layers = 1;
        if (vkCreateFramebuffer(device_, &fi, nullptr, &framebuffers_[i]) != VK_SUCCESS)
            return false;
    }
    return true;
}

// ---- create_command_pool ----
bool VulkanRenderer::create_command_pool()
{
    VkCommandPoolCreateInfo ci {};
    ci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    ci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    ci.queueFamilyIndex = queue_family_;
    return vkCreateCommandPool(device_, &ci, nullptr, &cmd_pool_) == VK_SUCCESS;
}

// ---- create_sync_objects ----
bool VulkanRenderer::create_sync_objects()
{
    VkSemaphoreCreateInfo si {};
    si.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fi {};
    fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    return vkCreateSemaphore(device_, &si, nullptr, &sem_image_available_) == VK_SUCCESS
        && vkCreateSemaphore(device_, &si, nullptr, &sem_render_finished_) == VK_SUCCESS
        && vkCreateFence(device_, &fi, nullptr, &fence_in_flight_) == VK_SUCCESS;
}

// ---- create_sampler ----
bool VulkanRenderer::create_sampler()
{
    VkSamplerCreateInfo si {};
    si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    si.magFilter = VK_FILTER_LINEAR;
    si.minFilter = VK_FILTER_LINEAR;
    si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.maxLod = 1.0f;

    return vkCreateSampler(device_, &si, nullptr, &sampler_) == VK_SUCCESS;
}

// ---- create_descriptor_pool_and_set ----
bool VulkanRenderer::create_descriptor_pool_and_set()
{
    VkDescriptorPoolSize pool_size {};
    pool_size.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    pool_size.descriptorCount = 1;

    VkDescriptorPoolCreateInfo pi {};
    pi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pi.maxSets = 1;
    pi.poolSizeCount = 1;
    pi.pPoolSizes = &pool_size;

    if (vkCreateDescriptorPool(device_, &pi, nullptr, &desc_pool_) != VK_SUCCESS)
        return false;

    VkDescriptorSetAllocateInfo ai {};
    ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool = desc_pool_;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts = &desc_set_layout_;

    return vkAllocateDescriptorSets(device_, &ai, &desc_set_) == VK_SUCCESS;
}

// ---- update_descriptor_set ----
void VulkanRenderer::update_descriptor_set(VkImageView view)
{
    VkDescriptorImageInfo img_info {};
    img_info.sampler = sampler_;
    img_info.imageView = view;
    img_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet write {};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = desc_set_;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &img_info;

    vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
}

// ---- cleanup_swapchain ----
void VulkanRenderer::cleanup_swapchain()
{
    for (auto fb : framebuffers_) vkDestroyFramebuffer(device_, fb, nullptr);
    framebuffers_.clear();
    for (auto v : swapchain_views_) vkDestroyImageView(device_, v, nullptr);
    swapchain_views_.clear();
    swapchain_images_.clear();
    if (swapchain_) { vkDestroySwapchainKHR(device_, swapchain_, nullptr); swapchain_ = VK_NULL_HANDLE; }
}

// ---- compute_letterbox ----
void VulkanRenderer::compute_letterbox(int frame_w, int frame_h, float out[4]) const
{
    float vp_aspect = static_cast<float>(swapchain_extent_.width) / static_cast<float>(swapchain_extent_.height);
    float frame_aspect = static_cast<float>(frame_w) / static_cast<float>(frame_h);

    float scale_x, scale_y, offset_x, offset_y;
    if (frame_aspect > vp_aspect) {
        // Wider than viewport: pillarbox (bars top/bottom)
        scale_x = 1.0f;
        scale_y = vp_aspect / frame_aspect;
        offset_x = 0.0f;
        offset_y = (1.0f - scale_y) * 0.5f;
    } else {
        // Taller than viewport: letterbox (bars left/right)
        scale_x = frame_aspect / vp_aspect;
        scale_y = 1.0f;
        offset_x = (1.0f - scale_x) * 0.5f;
        offset_y = 0.0f;
    }

    out[0] = scale_x;  // pc.scale.x
    out[1] = scale_y;  // pc.scale.y
    out[2] = offset_x; // pc.offset.x
    out[3] = offset_y; // pc.offset.y
}
