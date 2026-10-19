#pragma once

#include <vk_mem_alloc.h>

#include <utility>

namespace world_explorer::detail {

struct gpu_buffer {
    VmaAllocator allocator {};
    VkBuffer buffer {};
    VmaAllocation allocation {};
    VkDeviceSize bytes {};
    VkDeviceSize allocation_bytes {};
    void* mapped {};

    gpu_buffer() = default;
    gpu_buffer(const gpu_buffer&) = delete;
    gpu_buffer& operator=(const gpu_buffer&) = delete;

    gpu_buffer(gpu_buffer&& other) noexcept {
        swap(other);
    }

    gpu_buffer& operator=(gpu_buffer&& other) noexcept {
        gpu_buffer temporary(std::move(other));
        swap(temporary);
        return *this;
    }

    ~gpu_buffer() {
        if (buffer)
            vmaDestroyBuffer(allocator, buffer, allocation);
    }

    void swap(gpu_buffer& other) noexcept {
        std::swap(allocator, other.allocator);
        std::swap(buffer, other.buffer);
        std::swap(allocation, other.allocation);
        std::swap(bytes, other.bytes);
        std::swap(allocation_bytes, other.allocation_bytes);
        std::swap(mapped, other.mapped);
    }
};

struct gpu_image {
    VmaAllocator m_allocator {};
    VkImage m_image {};
    VmaAllocation m_allocation {};
    VkDeviceSize m_allocation_bytes {};
    gpu_image() = default;
    gpu_image(const gpu_image&) = delete;
    gpu_image& operator=(const gpu_image&) = delete;

    gpu_image(gpu_image&& other) noexcept {
        swap(other);
    }

    gpu_image& operator=(gpu_image&& other) noexcept {
        gpu_image temporary(std::move(other));
        swap(temporary);
        return *this;
    }

    ~gpu_image() {
        if (m_image)
            vmaDestroyImage(m_allocator, m_image, m_allocation);
    }

    void swap(gpu_image& other) noexcept {
        std::swap(m_allocator, other.m_allocator);
        std::swap(m_image, other.m_image);
        std::swap(m_allocation, other.m_allocation);
        std::swap(m_allocation_bytes, other.m_allocation_bytes);
    }
};

}  // namespace world_explorer::detail
