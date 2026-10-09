#include "VulkanPipeline.h"
#include "ShaderBlobs.h"
#include "VulkanContext.h"
#include "../core/Geometry.h"
#include "../core/RenderList.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <vector>

namespace cs {

namespace {

enum class VertexInput { None, MeshInstanced, Particle };
enum class Blend { Opaque, Alpha, Additive, Premultiplied };

struct PipelineDesc {
    const uint32_t* vs; size_t vsSize;
    const uint32_t* fs; size_t fsSize;
    VertexInput input = VertexInput::MeshInstanced;
    VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkCullModeFlags cull = VK_CULL_MODE_NONE;
    bool depthTest = false, depthWrite = false;
    Blend blend = Blend::Opaque;
    VkRenderPass pass;
    VkPipelineLayout layout;
};

VkShaderModule makeModule(VkDevice device, const uint32_t* code, size_t bytes) {
    VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    ci.codeSize = bytes;
    ci.pCode = code;
    VkShaderModule m = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &ci, nullptr, &m) != VK_SUCCESS) CS_LOGE("vkCreateShaderModule failed");
    return m;
}

VkPipeline buildPipeline(VkDevice device, VkPipelineCache cache, const PipelineDesc& d) {
    const VkShaderModule vs = makeModule(device, d.vs, d.vsSize), fs = makeModule(device, d.fs, d.fsSize);
    if (!vs || !fs) return VK_NULL_HANDLE;
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vs, "main", nullptr};
    stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fs, "main", nullptr};

    std::vector<VkVertexInputBindingDescription> bindings;
    std::vector<VkVertexInputAttributeDescription> attrs;
    if (d.input == VertexInput::MeshInstanced) {
        bindings.push_back({0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX});
        bindings.push_back({1, sizeof(Instance), VK_VERTEX_INPUT_RATE_INSTANCE});
        attrs.push_back({0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, pos)});
        attrs.push_back({1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal)});
        attrs.push_back({2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv)});
        for (uint32_t i = 0; i < 8; ++i) attrs.push_back({3 + i, 1, VK_FORMAT_R32G32B32A32_SFLOAT, i * 16}); // model cols, color, emissive, rim, params
    } else if (d.input == VertexInput::Particle) {
        bindings.push_back({0, sizeof(Particle), VK_VERTEX_INPUT_RATE_INSTANCE});
        attrs.push_back({0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 0});
        attrs.push_back({1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 16});
    }
    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vi.vertexBindingDescriptionCount = static_cast<uint32_t>(bindings.size());
    vi.pVertexBindingDescriptions = bindings.data();
    vi.vertexAttributeDescriptionCount = static_cast<uint32_t>(attrs.size());
    vi.pVertexAttributeDescriptions = attrs.data();

    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = d.topology;
    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = 1;
    vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = d.cull;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE; // three.js winding + the Y-flipped projection
    rs.lineWidth = 1.f;
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    ds.depthTestEnable = d.depthTest;
    ds.depthWriteEnable = d.depthWrite;
    ds.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

    VkPipelineColorBlendAttachmentState att{};
    att.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    if (d.blend != Blend::Opaque) {
        att.blendEnable = VK_TRUE;
        att.srcColorBlendFactor = d.blend == Blend::Premultiplied ? VK_BLEND_FACTOR_ONE : VK_BLEND_FACTOR_SRC_ALPHA; // three.js Normal / Additive
        att.dstColorBlendFactor = d.blend == Blend::Additive ? VK_BLEND_FACTOR_ONE : VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        att.colorBlendOp = VK_BLEND_OP_ADD;
        att.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        att.dstAlphaBlendFactor = d.blend == Blend::Additive ? VK_BLEND_FACTOR_ONE : VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        att.alphaBlendOp = VK_BLEND_OP_ADD;
    }
    VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cb.attachmentCount = 1;
    cb.pAttachments = &att;
    const VkDynamicState dyn[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dy{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dy.dynamicStateCount = 2;
    dy.pDynamicStates = dyn;

    VkGraphicsPipelineCreateInfo ci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    ci.stageCount = 2;
    ci.pStages = stages;
    ci.pVertexInputState = &vi;
    ci.pInputAssemblyState = &ia;
    ci.pViewportState = &vp;
    ci.pRasterizationState = &rs;
    ci.pMultisampleState = &ms;
    ci.pDepthStencilState = &ds;
    ci.pColorBlendState = &cb;
    ci.pDynamicState = &dy;
    ci.layout = d.layout;
    ci.renderPass = d.pass;
    VkPipeline p = VK_NULL_HANDLE;
    if (vkCreateGraphicsPipelines(device, cache, 1, &ci, nullptr, &p) != VK_SUCCESS) CS_LOGE("vkCreateGraphicsPipelines failed");
    vkDestroyShaderModule(device, vs, nullptr);
    vkDestroyShaderModule(device, fs, nullptr);
    return p;
}

// One colour attachment (+ optional transient depth). finalLayout: SHADER_READ for offscreen, PRESENT for swapchain.
VkRenderPass makePass(VkDevice device, VkFormat color, VkImageLayout finalLayout, VkFormat depth) {
    std::array<VkAttachmentDescription, 2> att{};
    att[0].format = color;
    att[0].samples = VK_SAMPLE_COUNT_1_BIT;
    att[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    att[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE; // sampled by the next pass / presented
    att[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    att[0].finalLayout = finalLayout;
    att[1].format = depth;
    att[1].samples = VK_SAMPLE_COUNT_1_BIT;
    att[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    att[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE; // never written back to memory on TBDR
    att[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    att[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    att[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    const bool hasDepth = depth != VK_FORMAT_UNDEFINED;
    VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference depthRef{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub{};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments = &colorRef;
    sub.pDepthStencilAttachment = hasDepth ? &depthRef : nullptr;

    const bool present = finalLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    std::array<VkSubpassDependency, 2> deps{};
    // in: wait for earlier reads of this target (previous frame's sampling) and for the acquire semaphore
    deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    deps[0].dstSubpass = 0;
    deps[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT |
                           (present ? 0 : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    deps[0].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    // out: make the colour writes visible to the next pass's fragment shader
    deps[1].srcSubpass = 0;
    deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    deps[1].dstStageMask = present ? VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    deps[1].dstAccessMask = present ? 0 : VK_ACCESS_SHADER_READ_BIT;

    VkRenderPassCreateInfo ci{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    ci.attachmentCount = hasDepth ? 2 : 1;
    ci.pAttachments = att.data();
    ci.subpassCount = 1;
    ci.pSubpasses = &sub;
    ci.dependencyCount = 2;
    ci.pDependencies = deps.data();
    VkRenderPass rp = VK_NULL_HANDLE;
    if (vkCreateRenderPass(device, &ci, nullptr, &rp) != VK_SUCCESS) CS_LOGE("vkCreateRenderPass failed");
    return rp;
}

VkDescriptorSetLayout makeSetLayout(VkDevice device, VkDescriptorType type, uint32_t count, VkShaderStageFlags stages) {
    std::vector<VkDescriptorSetLayoutBinding> b(count);
    for (uint32_t i = 0; i < count; ++i) b[i] = {i, type, 1, stages, nullptr};
    VkDescriptorSetLayoutCreateInfo ci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    ci.bindingCount = count;
    ci.pBindings = b.data();
    VkDescriptorSetLayout l = VK_NULL_HANDLE;
    vkCreateDescriptorSetLayout(device, &ci, nullptr, &l);
    return l;
}

VkPipelineLayout makeLayout(VkDevice device, VkDescriptorSetLayout set, uint32_t pushBytes) {
    VkPushConstantRange range{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, pushBytes}; // <= 128 guaranteed
    VkPipelineLayoutCreateInfo ci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    ci.setLayoutCount = 1;
    ci.pSetLayouts = &set;
    ci.pushConstantRangeCount = 1;
    ci.pPushConstantRanges = &range;
    VkPipelineLayout l = VK_NULL_HANDLE;
    vkCreatePipelineLayout(device, &ci, nullptr, &l);
    return l;
}

} // namespace

bool VulkanPipeline::create(VulkanContext& ctx, VkFormat hdrFormat, VkFormat presentFormat, const std::string& cachePath) {
    ctx_ = &ctx;
    VkDevice dev = ctx.device;
    // Pipeline cache from the last launch: lets the driver skip compiling SPIR-V to GPU code again.
    // Only reused when the header matches this exact GPU + driver (some mobile drivers mishandle foreign data).
    std::vector<char> blob;
    if (!cachePath.empty())
        if (FILE* f = std::fopen(cachePath.c_str(), "rb")) {
            std::fseek(f, 0, SEEK_END);
            const long n = std::ftell(f);
            std::fseek(f, 0, SEEK_SET);
            if (n > 32) { blob.resize(size_t(n)); if (std::fread(blob.data(), 1, blob.size(), f) != blob.size()) blob.clear(); }
            std::fclose(f);
        }
    if (blob.size() > 32) {
        uint32_t header[4];
        std::memcpy(header, blob.data(), sizeof header); // length, version, vendorID, deviceID, then the cache UUID
        const bool match = header[1] == VK_PIPELINE_CACHE_HEADER_VERSION_ONE && header[2] == ctx.properties.vendorID &&
                           header[3] == ctx.properties.deviceID &&
                           std::memcmp(blob.data() + 16, ctx.properties.pipelineCacheUUID, VK_UUID_SIZE) == 0;
        if (!match) blob.clear();
    }
    VkPipelineCacheCreateInfo pc{VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
    pc.initialDataSize = blob.size();
    pc.pInitialData = blob.empty() ? nullptr : blob.data();
    if (vkCreatePipelineCache(dev, &pc, nullptr, &cache_) != VK_SUCCESS) { // corrupt data: start empty
        pc.initialDataSize = 0; pc.pInitialData = nullptr;
        vkCreatePipelineCache(dev, &pc, nullptr, &cache_);
    }
    CS_LOGI("Pipeline cache: %s (%zu bytes)", blob.empty() ? "cold" : "warm", blob.size());

    scenePass = makePass(dev, hdrFormat, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, ctx.depthFormat);
    bloomPass = makePass(dev, hdrFormat, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_FORMAT_UNDEFINED);
    {   // binding 0: Frame UBO, 1: baked pattern textures (puddle mask), 2 / 3: the textured hen's albedo / normal map
        const VkDescriptorSetLayoutBinding b[4] = {
            {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
            {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
            {2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
            {3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}};
        VkDescriptorSetLayoutCreateInfo ci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        ci.bindingCount = 4;
        ci.pBindings = b;
        vkCreateDescriptorSetLayout(dev, &ci, nullptr, &frameSetLayout);
    }
    compositeSetLayout = makeSetLayout(dev, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2, VK_SHADER_STAGE_FRAGMENT_BIT);
    bloomSetLayout = makeSetLayout(dev, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT);
    sceneLayout = makeLayout(dev, frameSetLayout, sizeof(ScenePush));
    compositeLayout = makeLayout(dev, compositeSetLayout, sizeof(PostPush));
    bloomLayout = makeLayout(dev, bloomSetLayout, sizeof(PostPush));
    textLayout = makeLayout(dev, bloomSetLayout, sizeof(ScenePush));
    if (!scenePass || !bloomPass || !sceneLayout || !compositeLayout || !bloomLayout || !textLayout) return false;

    PipelineDesc d{};
    d.pass = scenePass; d.layout = sceneLayout;

    auto set = [&](const uint32_t* vs, size_t vsz, const uint32_t* fs, size_t fsz) { d.vs = vs; d.vsSize = vsz; d.fs = fs; d.fsSize = fsz; };

    set(spv::sky_vert, spv::sky_vert_size, spv::sky_frag, spv::sky_frag_size);
    d.input = VertexInput::None;
    sky = buildPipeline(dev, cache_, d);

    set(spv::lit_vert, spv::lit_vert_size, spv::lit_frag, spv::lit_frag_size);
    d.input = VertexInput::MeshInstanced; d.cull = VK_CULL_MODE_BACK_BIT; d.depthTest = true; d.depthWrite = true;
    lit = buildPipeline(dev, cache_, d);
    d.cull = VK_CULL_MODE_NONE;
    litTwoSided = buildPipeline(dev, cache_, d);

    set(spv::unlit_vert, spv::unlit_vert_size, spv::unlit_frag, spv::unlit_frag_size);
    d.cull = VK_CULL_MODE_NONE; d.depthWrite = false; d.blend = Blend::Alpha;
    unlitAlpha = buildPipeline(dev, cache_, d);
    d.blend = Blend::Additive;
    unlitAdd = buildPipeline(dev, cache_, d);

    set(spv::particle_vert, spv::particle_vert_size, spv::particle_frag, spv::particle_frag_size);
    d.input = VertexInput::Particle; d.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    particleAdd = buildPipeline(dev, cache_, d);
    d.blend = Blend::Alpha;
    particleAlpha = buildPipeline(dev, cache_, d);

    PipelineDesc b{};
    b.vs = spv::fullscreen_vert; b.vsSize = spv::fullscreen_vert_size;
    b.fs = spv::bloom_frag; b.fsSize = spv::bloom_frag_size;
    b.input = VertexInput::None; b.pass = bloomPass; b.layout = bloomLayout;
    bloom = buildPipeline(dev, cache_, b);

    PipelineDesc t{};
    t.vs = spv::text_vert; t.vsSize = spv::text_vert_size;
    t.fs = spv::text_frag; t.fsSize = spv::text_frag_size;
    t.input = VertexInput::MeshInstanced; t.blend = Blend::Alpha; t.depthTest = true;
    t.pass = scenePass; t.layout = textLayout;
    textWorld = buildPipeline(dev, cache_, t);

    if (!sky || !lit || !litTwoSided || !unlitAlpha || !unlitAdd || !particleAdd || !particleAlpha || !bloom || !textWorld) return false;
    return createPresentObjects(presentFormat);
}

bool VulkanPipeline::createPresentObjects(VkFormat presentFormat) {
    VkDevice dev = ctx_->device;
    presentPass = makePass(dev, presentFormat, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_FORMAT_UNDEFINED);
    if (!presentPass) return false;
    PipelineDesc c{};
    c.vs = spv::fullscreen_vert; c.vsSize = spv::fullscreen_vert_size;
    c.fs = spv::composite_frag; c.fsSize = spv::composite_frag_size;
    c.input = VertexInput::None; c.pass = presentPass; c.layout = compositeLayout;
    composite = buildPipeline(dev, cache_, c);

    PipelineDesc h{};
    h.vs = spv::unlit_vert; h.vsSize = spv::unlit_vert_size;
    h.fs = spv::unlit_frag; h.fsSize = spv::unlit_frag_size;
    h.input = VertexInput::MeshInstanced; h.blend = Blend::Alpha; h.pass = presentPass; h.layout = sceneLayout;
    hud = buildPipeline(dev, cache_, h);

    PipelineDesc t{};
    t.vs = spv::text_vert; t.vsSize = spv::text_vert_size;
    t.fs = spv::text_frag; t.fsSize = spv::text_frag_size;
    t.input = VertexInput::MeshInstanced; t.blend = Blend::Alpha; t.pass = presentPass; t.layout = textLayout;
    textHud = buildPipeline(dev, cache_, t);

    PipelineDesc im = t; // same vertex path as text (atlas UV rect per instance), full-colour fragment
    im.fs = spv::image_frag; im.fsSize = spv::image_frag_size; im.blend = Blend::Premultiplied;
    hudImage = buildPipeline(dev, cache_, im);
    return composite && hud && textHud && hudImage;
}

void VulkanPipeline::saveCache(const std::string& cachePath) const {
    if (cachePath.empty() || !cache_) return;
    size_t n = 0;
    if (vkGetPipelineCacheData(ctx_->device, cache_, &n, nullptr) != VK_SUCCESS || n == 0) return;
    std::vector<char> data(n);
    if (vkGetPipelineCacheData(ctx_->device, cache_, &n, data.data()) != VK_SUCCESS) return;
    const std::string tmp = cachePath + ".tmp"; // write then rename: never leave a half-written cache behind
    if (FILE* f = std::fopen(tmp.c_str(), "wb")) {
        const bool ok = std::fwrite(data.data(), 1, n, f) == n;
        std::fclose(f);
        if (ok) std::rename(tmp.c_str(), cachePath.c_str());
    }
}

void VulkanPipeline::destroyPresentObjects() {
    VkDevice dev = ctx_->device;
    for (VkPipeline* p : {&composite, &hud, &textHud, &hudImage}) { if (*p) vkDestroyPipeline(dev, *p, nullptr); *p = VK_NULL_HANDLE; }
    if (presentPass) vkDestroyRenderPass(dev, presentPass, nullptr);
    presentPass = VK_NULL_HANDLE;
}

bool VulkanPipeline::recreatePresent(VkFormat presentFormat) {
    destroyPresentObjects();
    return createPresentObjects(presentFormat);
}

void VulkanPipeline::destroy() {
    if (!ctx_) return;
    VkDevice dev = ctx_->device;
    destroyPresentObjects();
    for (VkPipeline* p : {&sky, &lit, &litTwoSided, &unlitAlpha, &unlitAdd, &particleAdd, &particleAlpha, &bloom, &textWorld}) {
        if (*p) vkDestroyPipeline(dev, *p, nullptr);
        *p = VK_NULL_HANDLE;
    }
    for (VkPipelineLayout* l : {&sceneLayout, &compositeLayout, &bloomLayout, &textLayout}) { if (*l) vkDestroyPipelineLayout(dev, *l, nullptr); *l = VK_NULL_HANDLE; }
    for (VkDescriptorSetLayout* l : {&frameSetLayout, &compositeSetLayout, &bloomSetLayout}) { if (*l) vkDestroyDescriptorSetLayout(dev, *l, nullptr); *l = VK_NULL_HANDLE; }
    for (VkRenderPass* r : {&scenePass, &bloomPass}) { if (*r) vkDestroyRenderPass(dev, *r, nullptr); *r = VK_NULL_HANDLE; }
    if (cache_) vkDestroyPipelineCache(dev, cache_, nullptr);
    cache_ = VK_NULL_HANDLE;
    ctx_ = nullptr;
}

} // namespace cs
