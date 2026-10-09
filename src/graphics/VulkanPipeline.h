// Render passes, descriptor set layouts, pipeline layouts and every graphics pipeline (PSO).
// Classic VkRenderPass/VkFramebuffer (no VK_KHR_dynamic_rendering). TBDR rule for every attachment:
// LOAD_OP_CLEAR on load; depth/stencil STORE_OP_DONT_CARE so it never leaves tile memory.
#pragma once

#include "VulkanCommon.h"

#include <string>

namespace cs {

class VulkanContext;

// Push-constant blocks (must match the shaders)
struct ScenePush { float viewProj[16]; float kind[4]; };           // unlit.vert / particle.frag (80 bytes)
struct PostPush { float rot[4]; float params[4]; float extra[4]; float frost[4]; float frost2[4]; }; // fullscreen.vert / composite / bloom (80 bytes)

class VulkanPipeline {
public:
    // cachePath: on-disk VkPipelineCache (driver-compiled shaders). Empty = no persistence.
    bool create(VulkanContext& ctx, VkFormat hdrFormat, VkFormat presentFormat, const std::string& cachePath = {});
    // Writes the pipeline cache to cachePath (call once pipelines exist).
    void saveCache(const std::string& cachePath) const;
    void destroy();
    // the present pass depends on the swapchain format, which may change when the surface is recreated
    bool recreatePresent(VkFormat presentFormat);

    VkRenderPass scenePass = VK_NULL_HANDLE;   // HDR colour + transient depth
    VkRenderPass bloomPass = VK_NULL_HANDLE;   // quarter-res HDR colour
    VkRenderPass presentPass = VK_NULL_HANDLE; // swapchain image

    VkDescriptorSetLayout frameSetLayout = VK_NULL_HANDLE;     // binding 0: Frame UBO, binding 1: puddle mask
    VkDescriptorSetLayout compositeSetLayout = VK_NULL_HANDLE; // binding 0: HDR, binding 1: bloom
    VkDescriptorSetLayout bloomSetLayout = VK_NULL_HANDLE;     // binding 0: source
    VkPipelineLayout sceneLayout = VK_NULL_HANDLE;
    VkPipelineLayout compositeLayout = VK_NULL_HANDLE;
    VkPipelineLayout bloomLayout = VK_NULL_HANDLE;
    VkPipelineLayout textLayout = VK_NULL_HANDLE; // set 0: font atlas (bloomSetLayout shape), ScenePush

    VkPipeline sky = VK_NULL_HANDLE, lit = VK_NULL_HANDLE, unlitAlpha = VK_NULL_HANDLE, unlitAdd = VK_NULL_HANDLE,
               particleAdd = VK_NULL_HANDLE, particleAlpha = VK_NULL_HANDLE, bloom = VK_NULL_HANDLE,
               composite = VK_NULL_HANDLE, hud = VK_NULL_HANDLE, textHud = VK_NULL_HANDLE, textWorld = VK_NULL_HANDLE,
               hudImage = VK_NULL_HANDLE;

private:
    bool createPresentObjects(VkFormat presentFormat);
    void destroyPresentObjects();
    VulkanContext* ctx_ = nullptr;
    VkPipelineCache cache_ = VK_NULL_HANDLE;
};

} // namespace cs
