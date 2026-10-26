#include "mesh_pass.hpp"

#include <array>
#include <cstddef>

namespace world_explorer {

using detail::check;
using detail::structure;

MeshTarget::~MeshTarget() {
    if (m_framebuffer) {
        vkDestroyFramebuffer(m_device, m_framebuffer, nullptr);
    }
    if (m_color_view) {
        vkDestroyImageView(m_device, m_color_view, nullptr);
    }
    if (m_depth_view) {
        vkDestroyImageView(m_device, m_depth_view, nullptr);
    }
}

std::size_t MeshTarget::allocation_bytes() const {
    return static_cast<std::size_t>(m_color.m_allocation_bytes + m_depth.m_allocation_bytes);
}

MeshPass::MeshPass(vulkan_device& device) : m_backend(device), m_device(device.device()) { }

MeshPass::~MeshPass() {
    if (m_pipeline) {
        vkDestroyPipeline(m_device, m_pipeline, nullptr);
    }
    if (m_layout) {
        vkDestroyPipelineLayout(m_device, m_layout, nullptr);
    }
    if (m_pass) {
        vkDestroyRenderPass(m_device, m_pass, nullptr);
    }
}

void MeshPass::initialize(const std::filesystem::path& shaders,
                          VkDescriptorSetLayout scene_layout,
                          bool pbr) {
    for (const auto K_FORMAT : { VK_FORMAT_R32G32B32A32_SFLOAT, VK_FORMAT_D32_SFLOAT }) {
        VkFormatProperties properties {};
        vkGetPhysicalDeviceFormatProperties(m_backend.physical_device(), K_FORMAT, &properties);
        const VkFormatFeatureFlags K_REQUIRED =
            K_FORMAT == VK_FORMAT_D32_SFLOAT
                ? VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT
                : VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_TRANSFER_SRC_BIT;
        if ((properties.optimalTilingFeatures & K_REQUIRED) != K_REQUIRED) {
            throw std::runtime_error(
                "Mesh diagnostic requires FP32 color and D32 depth attachments");
        }
    }
    const std::array<VkAttachmentDescription, 2> K_ATTACHMENTS {
        { { 0,
          VK_FORMAT_R32G32B32A32_SFLOAT,
          VK_SAMPLE_COUNT_1_BIT,
          VK_ATTACHMENT_LOAD_OP_CLEAR,
          VK_ATTACHMENT_STORE_OP_STORE,
          VK_ATTACHMENT_LOAD_OP_DONT_CARE,
          VK_ATTACHMENT_STORE_OP_DONT_CARE,
          VK_IMAGE_LAYOUT_UNDEFINED,
          VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL },
         { 0,
          VK_FORMAT_D32_SFLOAT,
          VK_SAMPLE_COUNT_1_BIT,
          VK_ATTACHMENT_LOAD_OP_CLEAR,
          VK_ATTACHMENT_STORE_OP_DONT_CARE,
          VK_ATTACHMENT_LOAD_OP_DONT_CARE,
          VK_ATTACHMENT_STORE_OP_DONT_CARE,
          VK_IMAGE_LAYOUT_UNDEFINED,
          VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL } } };
    const VkAttachmentReference K_COLOR { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    const VkAttachmentReference K_DEPTH { 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
    VkSubpassDescription subpass {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &K_COLOR;
    subpass.pDepthStencilAttachment = &K_DEPTH;
    const std::array<VkSubpassDependency, 2> K_DEPENDENCIES {
        { { VK_SUBPASS_EXTERNAL,
          0,
          VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
          VkPipelineStageFlags { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT } |
              VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
              VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
          VkAccessFlags { VK_ACCESS_MEMORY_READ_BIT } | VK_ACCESS_MEMORY_WRITE_BIT,
          VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
          VK_DEPENDENCY_BY_REGION_BIT },
         { 0,
          VK_SUBPASS_EXTERNAL,
          VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
          VK_PIPELINE_STAGE_TRANSFER_BIT,
          VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
          VK_ACCESS_TRANSFER_READ_BIT,
          VK_DEPENDENCY_BY_REGION_BIT } } };
    auto pass = structure<VkRenderPassCreateInfo>(VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO);
    pass.attachmentCount = 2;
    pass.pAttachments = K_ATTACHMENTS.data();
    pass.subpassCount = 1;
    pass.pSubpasses = &subpass;
    pass.dependencyCount = 2;
    pass.pDependencies = K_DEPENDENCIES.data();
    check(vkCreateRenderPass(m_device, &pass, nullptr, &m_pass),
          "create mesh diagnostic render pass");
    const VkPushConstantRange K_PUSH { VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                      0,
                                      sizeof(detail::push_parameters) };
    auto layout =
        structure<VkPipelineLayoutCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO);
    layout.setLayoutCount = 1;
    layout.pSetLayouts = &scene_layout;
    layout.pushConstantRangeCount = 1;
    layout.pPushConstantRanges = &K_PUSH;
    check(vkCreatePipelineLayout(m_device, &layout, nullptr, &m_layout),
          "create mesh diagnostic layout");
    const auto K_VERTEX = m_backend.load_shader_module(shaders, pbr ? "pbr.vert" : "mesh.vert");
    VkShaderModule fragment {};
    try {
        fragment = m_backend.load_shader_module(shaders, pbr ? "pbr.frag" : "mesh.frag");
        const std::array<VkPipelineShaderStageCreateInfo, 2> K_STAGES {
            { { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
              nullptr,
              0,
              VK_SHADER_STAGE_VERTEX_BIT,
              K_VERTEX,
              "main",
              nullptr },
             { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
              nullptr,
              0,
              VK_SHADER_STAGE_FRAGMENT_BIT,
              fragment,
              "main",
              nullptr } } };
        const VkVertexInputBindingDescription K_BINDING {
            0,
            static_cast<std::uint32_t>(pbr ? sizeof(PbrGpuVertex) : sizeof(MeshGpuVertex)),
            VK_VERTEX_INPUT_RATE_VERTEX };
        const std::array<VkVertexInputAttributeDescription, 4> K_ATTRIBUTES =
            pbr ? std::array<
                      VkVertexInputAttributeDescription,
                      4> { { { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(PbrGpuVertex, m_position) },
                           { 1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(PbrGpuVertex, m_normal) },
                           { 2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(PbrGpuVertex, m_tangent) },
                           { 3, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(PbrGpuVertex, m_uv) } } }
                : std::array<VkVertexInputAttributeDescription, 4> {
                      { { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(MeshGpuVertex, m_position) },
                       { 1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(MeshGpuVertex, m_normal) },
                       { 2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(MeshGpuVertex, m_uv) },
                       { 3, 0, VK_FORMAT_R32G32B32_UINT, offsetof(MeshGpuVertex, m_texture) } } };
        auto vertices = structure<VkPipelineVertexInputStateCreateInfo>(
            VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO);
        vertices.vertexBindingDescriptionCount = 1;
        vertices.pVertexBindingDescriptions = &K_BINDING;
        vertices.vertexAttributeDescriptionCount = 4;
        vertices.pVertexAttributeDescriptions = K_ATTRIBUTES.data();
        auto assembly = structure<VkPipelineInputAssemblyStateCreateInfo>(
            VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO);
        assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        auto viewport = structure<VkPipelineViewportStateCreateInfo>(
            VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO);
        viewport.viewportCount = 1;
        viewport.scissorCount = 1;
        auto raster = structure<VkPipelineRasterizationStateCreateInfo>(
            VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO);
        raster.polygonMode = VK_POLYGON_MODE_FILL;
        raster.cullMode = VK_CULL_MODE_NONE;
        raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        raster.lineWidth = 1;
        auto samples = structure<VkPipelineMultisampleStateCreateInfo>(
            VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO);
        samples.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        auto depth = structure<VkPipelineDepthStencilStateCreateInfo>(
            VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO);
        depth.depthTestEnable = VK_TRUE;
        depth.depthWriteEnable = VK_TRUE;
        depth.depthCompareOp = VK_COMPARE_OP_LESS;
        VkPipelineColorBlendAttachmentState blend {};
        blend.colorWriteMask = VkColorComponentFlags { VK_COLOR_COMPONENT_R_BIT } |
                               VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
                               VK_COLOR_COMPONENT_A_BIT;
        auto blending = structure<VkPipelineColorBlendStateCreateInfo>(
            VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO);
        blending.attachmentCount = 1;
        blending.pAttachments = &blend;
        const std::array<VkDynamicState, 2> K_DYNAMIC { VK_DYNAMIC_STATE_VIEWPORT,
                                                       VK_DYNAMIC_STATE_SCISSOR };
        auto dynamic = structure<VkPipelineDynamicStateCreateInfo>(
            VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO);
        dynamic.dynamicStateCount = 2;
        dynamic.pDynamicStates = K_DYNAMIC.data();
        auto info = structure<VkGraphicsPipelineCreateInfo>(
            VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO);
        info.stageCount = 2;
        info.pStages = K_STAGES.data();
        info.pVertexInputState = &vertices;
        info.pInputAssemblyState = &assembly;
        info.pViewportState = &viewport;
        info.pRasterizationState = &raster;
        info.pMultisampleState = &samples;
        info.pDepthStencilState = &depth;
        info.pColorBlendState = &blending;
        info.pDynamicState = &dynamic;
        info.layout = m_layout;
        info.renderPass = m_pass;
        check(vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &info, nullptr, &m_pipeline),
              "create mesh diagnostic pipeline");
    } catch (...) {
        vkDestroyShaderModule(m_device, K_VERTEX, nullptr);
        if (fragment) {
            vkDestroyShaderModule(m_device, fragment, nullptr);
        }
        throw;
    }
    vkDestroyShaderModule(m_device, K_VERTEX, nullptr);
    vkDestroyShaderModule(m_device, fragment, nullptr);
}

std::unique_ptr<MeshTarget> MeshPass::create_target(VkExtent2D extent, std::size_t budget) {
    auto target = std::make_unique<MeshTarget>();
    target->m_device = m_device;
    target->m_extent = extent;
    const auto K_IMAGE = [&](VkFormat format, VkImageUsageFlags usage) {
        auto info = structure<VkImageCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO);
        info.imageType = VK_IMAGE_TYPE_2D;
        info.format = format;
        info.extent = { extent.width, extent.height, 1 };
        info.mipLevels = 1;
        info.arrayLayers = 1;
        info.samples = VK_SAMPLE_COUNT_1_BIT;
        info.tiling = VK_IMAGE_TILING_OPTIMAL;
        info.usage = usage;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        return m_backend.allocate_image(info, budget - target->allocation_bytes());
    };
    target->m_color =
        K_IMAGE(VK_FORMAT_R32G32B32A32_SFLOAT,
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
    target->m_depth = K_IMAGE(VK_FORMAT_D32_SFLOAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);
    const auto K_VIEW =
        [&](VkImage image, VkFormat format, VkImageAspectFlags aspect, VkImageView& output) {
            auto info = structure<VkImageViewCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO);
            info.image = image;
            info.viewType = VK_IMAGE_VIEW_TYPE_2D;
            info.format = format;
            info.subresourceRange = { aspect, 0, 1, 0, 1 };
            check(vkCreateImageView(m_device, &info, nullptr, &output), "create mesh target view");
        };
    K_VIEW(target->m_color.m_image,
           VK_FORMAT_R32G32B32A32_SFLOAT,
           VK_IMAGE_ASPECT_COLOR_BIT,
           target->m_color_view);
    K_VIEW(target->m_depth.m_image,
           VK_FORMAT_D32_SFLOAT,
           VK_IMAGE_ASPECT_DEPTH_BIT,
           target->m_depth_view);
    const std::array<VkImageView, 2> K_VIEWS { target->m_color_view, target->m_depth_view };
    auto framebuffer =
        structure<VkFramebufferCreateInfo>(VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO);
    framebuffer.renderPass = m_pass;
    framebuffer.attachmentCount = 2;
    framebuffer.pAttachments = K_VIEWS.data();
    framebuffer.width = extent.width;
    framebuffer.height = extent.height;
    framebuffer.layers = 1;
    check(vkCreateFramebuffer(m_device, &framebuffer, nullptr, &target->m_framebuffer),
          "create mesh framebuffer");
    return target;
}

void MeshPass::record(VkCommandBuffer command,
                      const MeshTarget& target,
                      const detail::gpu_buffer& scene,
                      const detail::push_parameters& parameters,
                      MeshDraw draw,
                      VkBuffer captured,
                      VkDescriptorSet scene_descriptor) {
    const std::array<VkClearValue, 2> K_CLEAR { VkClearValue { .color = { { .045F, .051F, .063F, 1 } } },
                                               VkClearValue { .depthStencil = { 1, 0 } } };
    auto pass = structure<VkRenderPassBeginInfo>(VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO);
    pass.renderPass = m_pass;
    pass.framebuffer = target.m_framebuffer;
    pass.renderArea.extent = target.m_extent;
    pass.clearValueCount = 2;
    pass.pClearValues = K_CLEAR.data();
    vkCmdBeginRenderPass(command, &pass, VK_SUBPASS_CONTENTS_INLINE);
    const VkViewport K_VIEWPORT { 0,
                                 0,
                                 static_cast<float>(target.m_extent.width),
                                 static_cast<float>(target.m_extent.height),
                                 0,
                                 1 };
    const VkRect2D K_SCISSOR { { 0, 0 }, target.m_extent };
    vkCmdSetViewport(command, 0, 1, &K_VIEWPORT);
    vkCmdSetScissor(command, 0, 1, &K_SCISSOR);
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
    auto mesh_parameters = parameters;
    mesh_parameters.count_shift[3] = draw.m_baked ? 1U : 0U;
    vkCmdBindDescriptorSets(
        command, VK_PIPELINE_BIND_POINT_GRAPHICS, m_layout, 0, 1, &scene_descriptor, 0, nullptr);
    vkCmdPushConstants(command,
                       m_layout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0,
                       sizeof(mesh_parameters),
                       &mesh_parameters);
    const VkDeviceSize K_VERTEX_OFFSET { draw.m_vertex_offset };
    vkCmdBindVertexBuffers(command, 0, 1, &scene.buffer, &K_VERTEX_OFFSET);
    vkCmdBindIndexBuffer(command, scene.buffer, draw.m_index_offset, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(command, draw.m_index_count, 1, 0, 0, 0);
    vkCmdEndRenderPass(command);
    detail::memory_barrier(command,
                           VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                           VK_ACCESS_2_SHADER_WRITE_BIT,
                           VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                           VK_ACCESS_2_TRANSFER_WRITE_BIT);
    VkBufferImageCopy copy {};
    copy.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    copy.imageExtent = { target.m_extent.width, target.m_extent.height, 1 };
    vkCmdCopyImageToBuffer(
        command, target.m_color.m_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, captured, 1, &copy);
    detail::memory_barrier(command,
                           VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                           VK_ACCESS_2_TRANSFER_WRITE_BIT,
                           VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                           VK_ACCESS_2_SHADER_READ_BIT);
}

void MeshPass::record_pbr(VkCommandBuffer command,
                          const MeshTarget& target,
                          const detail::gpu_buffer& scene,
                          const detail::push_parameters& parameters,
                          const std::vector<PbrDraw>& draws,
                          const PbrLighting& lighting,
                          VkBuffer captured,
                          VkDescriptorSet scene_descriptor) {
    const std::array<VkClearValue, 2> K_CLEAR { VkClearValue { .color = { { .045F, .051F, .063F, 1 } } },
                                               VkClearValue { .depthStencil = { 1, 0 } } };
    auto pass = structure<VkRenderPassBeginInfo>(VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO);
    pass.renderPass = m_pass;
    pass.framebuffer = target.m_framebuffer;
    pass.renderArea.extent = target.m_extent;
    pass.clearValueCount = 2;
    pass.pClearValues = K_CLEAR.data();
    vkCmdBeginRenderPass(command, &pass, VK_SUBPASS_CONTENTS_INLINE);
    const VkViewport K_VIEWPORT { 0,
                                 0,
                                 static_cast<float>(target.m_extent.width),
                                 static_cast<float>(target.m_extent.height),
                                 0,
                                 1 };
    const VkRect2D K_SCISSOR { { 0, 0 }, target.m_extent };
    vkCmdSetViewport(command, 0, 1, &K_VIEWPORT);
    vkCmdSetScissor(command, 0, 1, &K_SCISSOR);
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);

    struct Push {
        std::array<float, 4> m_right, m_down, m_forward, m_intrinsics, m_viewport;
        std::array<std::uint32_t, 4> m_draw;
        std::array<float, 4> m_light, m_ambient;
    };

    static_assert(sizeof(Push) == 128uz);
    const auto K_LENGTH = std::sqrt(dot(lighting.m_to_light, lighting.m_to_light));
    const auto K_DIRECTION = lighting.m_to_light * (1 / K_LENGTH);
    const auto K_DOT = [&](const auto& row) {
        return static_cast<float>(row[0uz] * K_DIRECTION.m_x + row[1uz] * K_DIRECTION.m_y +
                                  row[2uz] * K_DIRECTION.m_z);
    };
    Push push { parameters.right,
               parameters.down,
               parameters.forward,
               parameters.intrinsics,
               parameters.viewport_clip,
               {},
               { K_DOT(parameters.right),
                K_DOT(parameters.down),
                K_DOT(parameters.forward),
                static_cast<float>(lighting.m_intensity) },
               { static_cast<float>(lighting.m_ambient.m_x),
                static_cast<float>(lighting.m_ambient.m_y),
                static_cast<float>(lighting.m_ambient.m_z),
                static_cast<float>(lighting.m_exposure) } };
    vkCmdBindDescriptorSets(
        command, VK_PIPELINE_BIND_POINT_GRAPHICS, m_layout, 0, 1, &scene_descriptor, 0, nullptr);
    for (const auto& draw : draws) {
        push.m_draw = { draw.m_instance_words, draw.m_material_words, 0, 0 };
        vkCmdPushConstants(command,
                           m_layout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0,
                           sizeof(push),
                           &push);
        const VkDeviceSize K_OFFSET { draw.m_vertex_offset };
        vkCmdBindVertexBuffers(command, 0, 1, &scene.buffer, &K_OFFSET);
        vkCmdBindIndexBuffer(command, scene.buffer, draw.m_index_offset, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(command, draw.m_index_count, draw.m_instance_count, 0, 0, 0);
    }
    vkCmdEndRenderPass(command);
    detail::memory_barrier(command,
                           VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                           VK_ACCESS_2_SHADER_WRITE_BIT,
                           VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                           VK_ACCESS_2_TRANSFER_WRITE_BIT);
    VkBufferImageCopy copy {};
    copy.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    copy.imageExtent = { target.m_extent.width, target.m_extent.height, 1 };
    vkCmdCopyImageToBuffer(
        command, target.m_color.m_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, captured, 1, &copy);
    detail::memory_barrier(command,
                           VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                           VK_ACCESS_2_TRANSFER_WRITE_BIT,
                           VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                           VK_ACCESS_2_SHADER_READ_BIT);
}

}  // namespace world_explorer
