#include "vulkan_texture.hpp"

#include "utils/log.hpp"

bool VulkanTexture::init(VkDevice device,
    VkPhysicalDevice phys_device,
    VkCommandPool /*cmd_pool*/,
    VkQueue /*queue*/,
    int width,
    int height)
{
    device_ = device;
    phys_device_ = phys_device;
    width_ = width;
    height_ = height;
    first_flush_ = true;

    const VkDeviceSize size = static_cast<VkDeviceSize>(width) * height * 4;
    staging_size_ = size;

    // --- staging buffer (HOST_VISIBLE | HOST_COHERENT) ---
    {
        VkBufferCreateInfo bi {};
        bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bi.size = size;
        bi.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateBuffer(device_, &bi, nullptr, &staging_buf_) != VK_SUCCESS) {
            LOG_ERROR() << "VulkanTexture: failed to create staging buffer";
            return false;
        }

        VkMemoryRequirements req;
        vkGetBufferMemoryRequirements(device_, staging_buf_, &req);

        VkMemoryAllocateInfo ai {};
        ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize = req.size;
        ai.memoryTypeIndex = find_memory_type(req.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        if (ai.memoryTypeIndex == UINT32_MAX) {
            LOG_ERROR() << "VulkanTexture: no suitable memory type for staging";
            return false;
        }

        if (vkAllocateMemory(device_, &ai, nullptr, &staging_mem_) != VK_SUCCESS) {
            LOG_ERROR() << "VulkanTexture: failed to allocate staging memory";
            return false;
        }
        vkBindBufferMemory(device_, staging_buf_, staging_mem_, 0);

        void* ptr = nullptr;
        vkMapMemory(device_, staging_mem_, 0, size, 0, &ptr);
        mapped_ = static_cast<uint8_t*>(ptr);
    }

    // --- GPU image (DEVICE_LOCAL) ---
    {
        VkImageCreateInfo ii {};
        ii.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ii.imageType = VK_IMAGE_TYPE_2D;
        ii.format = VK_FORMAT_R8G8B8A8_UNORM;
        ii.extent = { static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1 };
        ii.mipLevels = 1;
        ii.arrayLayers = 1;
        ii.samples = VK_SAMPLE_COUNT_1_BIT;
        ii.tiling = VK_IMAGE_TILING_OPTIMAL;
        ii.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        ii.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        if (vkCreateImage(device_, &ii, nullptr, &image_) != VK_SUCCESS) {
            LOG_ERROR() << "VulkanTexture: failed to create image";
            return false;
        }

        VkMemoryRequirements req;
        vkGetImageMemoryRequirements(device_, image_, &req);

        VkMemoryAllocateInfo ai {};
        ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize = req.size;
        ai.memoryTypeIndex = find_memory_type(req.memoryTypeBits,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        if (ai.memoryTypeIndex == UINT32_MAX) {
            LOG_ERROR() << "VulkanTexture: no suitable memory type for image";
            return false;
        }

        if (vkAllocateMemory(device_, &ai, nullptr, &image_mem_) != VK_SUCCESS) {
            LOG_ERROR() << "VulkanTexture: failed to allocate image memory";
            return false;
        }
        vkBindImageMemory(device_, image_, image_mem_, 0);
    }

    // --- image view ---
    {
        VkImageViewCreateInfo vi {};
        vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image = image_;
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = VK_FORMAT_R8G8B8A8_UNORM;
        vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        vi.subresourceRange.levelCount = 1;
        vi.subresourceRange.layerCount = 1;

        if (vkCreateImageView(device_, &vi, nullptr, &image_view_) != VK_SUCCESS) {
            LOG_ERROR() << "VulkanTexture: failed to create image view";
            return false;
        }
    }

    return true;
}

void VulkanTexture::destroy()
{
    if (!device_) return;

    if (mapped_) {
        vkUnmapMemory(device_, staging_mem_);
        mapped_ = nullptr;
    }
    if (image_view_) { vkDestroyImageView(device_, image_view_, nullptr); image_view_ = VK_NULL_HANDLE; }
    if (image_) { vkDestroyImage(device_, image_, nullptr); image_ = VK_NULL_HANDLE; }
    if (image_mem_) { vkFreeMemory(device_, image_mem_, nullptr); image_mem_ = VK_NULL_HANDLE; }
    if (staging_buf_) { vkDestroyBuffer(device_, staging_buf_, nullptr); staging_buf_ = VK_NULL_HANDLE; }
    if (staging_mem_) { vkFreeMemory(device_, staging_mem_, nullptr); staging_mem_ = VK_NULL_HANDLE; }

    width_ = 0;
    height_ = 0;
    staging_size_ = 0;
    first_flush_ = true;
}

void VulkanTexture::flush_to_image(VkCommandBuffer cmd)
{
    // First flush: transition from UNDEFINED to TRANSFER_DST.
    // Subsequent flushes: transition from SHADER_READ_ONLY to TRANSFER_DST.
    VkImageLayout old_layout = first_flush_
        ? VK_IMAGE_LAYOUT_UNDEFINED
        : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    transition_layout(cmd, image_, old_layout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    VkBufferImageCopy region {};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = { static_cast<uint32_t>(width_),
        static_cast<uint32_t>(height_), 1 };

    vkCmdCopyBufferToImage(cmd, staging_buf_, image_,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    transition_layout(cmd, image_,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    first_flush_ = false;
}

void VulkanTexture::transition_layout(VkCommandBuffer cmd,
    VkImage image,
    VkImageLayout old_layout,
    VkImageLayout new_layout)
{
    VkImageMemoryBarrier barrier {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = old_layout;
    barrier.newLayout = new_layout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;

    VkPipelineStageFlags src_stage, dst_stage;

    if (old_layout == VK_IMAGE_LAYOUT_UNDEFINED
        && new_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dst_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (old_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        && new_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        src_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        dst_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (old_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
        && new_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        src_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dst_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else {
        src_stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        dst_stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    }

    vkCmdPipelineBarrier(cmd, src_stage, dst_stage, 0,
        0, nullptr, 0, nullptr, 1, &barrier);
}

uint32_t VulkanTexture::find_memory_type(uint32_t type_filter,
    VkMemoryPropertyFlags props) const
{
    VkPhysicalDeviceMemoryProperties mem_props;
    vkGetPhysicalDeviceMemoryProperties(phys_device_, &mem_props);

    for (uint32_t i = 0; i < mem_props.memoryTypeCount; ++i) {
        if ((type_filter & (1 << i))
            && (mem_props.memoryTypes[i].propertyFlags & props) == props) {
            return i;
        }
    }
    return UINT32_MAX;
}
