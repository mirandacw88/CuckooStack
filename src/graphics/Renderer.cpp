#include "Renderer.h"
#include "BakedPatterns.h"
#include "../core/Font.h"
#include "../core/HudImage.h"
#include "../core/Geometry.h"
#include "../platform/PlatformSurface.h"

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace cs {

namespace {

constexpr size_t kMaxInstances = 20000; // per frame, all instanced passes + HUD
constexpr size_t kMaxParticles = 4096;  // fxAdd (3200) + fxSmoke (700)

// std140 mirror of frame.glsl
struct GpuFrame {
    glm::mat4 view, proj, viewProj, invViewProj;
    glm::vec4 cameraPos, fogColor, skyTop, skyMid, skyBot, skyHaze, moonDir, hemiSky, hemiGround, sunDir, sunColor;
    glm::vec4 pointPos[3], pointColor[3];
    glm::vec4 misc;
};

GpuFrame toGpu(const FrameParams& f) {
    GpuFrame g{};
    g.view = f.view; g.proj = f.proj; g.viewProj = f.proj * f.view; g.invViewProj = glm::inverse(g.viewProj);
    g.cameraPos = glm::vec4(f.cameraPos, f.time);
    g.fogColor = glm::vec4(f.fogColor, f.fogDensity);
    g.skyTop = glm::vec4(f.skyTop, 0); g.skyMid = glm::vec4(f.skyMid, 0); g.skyBot = glm::vec4(f.skyBot, 0); g.skyHaze = glm::vec4(f.skyHaze, 0);
    g.moonDir = glm::vec4(f.moonDir, 0);
    g.hemiSky = glm::vec4(f.hemiSky, f.hemiIntensity); g.hemiGround = glm::vec4(f.hemiGround, 0);
    g.sunDir = glm::vec4(f.sunDir, 0); g.sunColor = glm::vec4(f.sunColor, 0);
    for (int i = 0; i < 3; ++i) {
        g.pointPos[i] = glm::vec4(f.points[i].pos, f.points[i].distance);
        g.pointColor[i] = glm::vec4(f.points[i].color * f.points[i].intensity, 0);
    }
    g.misc = glm::vec4(f.pulse, 0, 0, 0);
    return g;
}

void setViewport(VkCommandBuffer cmd, VkExtent2D e) {
    VkViewport vp{0, 0, float(e.width), float(e.height), 0, 1};
    VkRect2D sc{{0, 0}, e};
    vkCmdSetViewport(cmd, 0, 1, &vp);
    vkCmdSetScissor(cmd, 0, 1, &sc);
}

VkFramebuffer makeFramebuffer(VkDevice dev, VkRenderPass pass, std::initializer_list<VkImageView> views, VkExtent2D e) {
    VkFramebufferCreateInfo ci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    ci.renderPass = pass;
    ci.attachmentCount = static_cast<uint32_t>(views.size());
    ci.pAttachments = views.begin();
    ci.width = e.width;
    ci.height = e.height;
    ci.layers = 1;
    VkFramebuffer fb = VK_NULL_HANDLE;
    if (vkCreateFramebuffer(dev, &ci, nullptr, &fb) != VK_SUCCESS) CS_LOGE("vkCreateFramebuffer failed");
    return fb;
}

} // namespace

bool Renderer::init(PlatformSurface& platform, bool enableValidation, std::string& error) {
    platform_ = &platform;
    VulkanContext::Desc desc;
    desc.enableValidation = enableValidation;
    if (!ctx_.init(platform, desc, error)) return false;

    // RGBA16F colour attachment + linear filtering is mandatory in Vulkan 1.0; keep a fallback for broken drivers.
    VkFormatProperties fp;
    vkGetPhysicalDeviceFormatProperties(ctx_.gpu, hdrFormat_, &fp);
    const VkFormatFeatureFlags need = VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT |
                                      VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    if ((fp.optimalTilingFeatures & need) != need) {
        CS_LOGW("RGBA16F render target unsupported; falling back to LDR RGBA8");
        hdrFormat_ = VK_FORMAT_R8G8B8A8_UNORM;
    }

    if (!swapchain_.create(ctx_, platform.drawableExtent())) { error = "Swapchain creation failed"; return false; }
    presentFormat_ = swapchain_.format();
    if (!pipes_.create(ctx_, hdrFormat_, presentFormat_, pipelineCachePath_)) { error = "Pipeline creation failed"; return false; }
    pipes_.saveCache(pipelineCachePath_);

    VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    si.magFilter = si.minFilter = VK_FILTER_LINEAR;
    si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.maxLod = 0.f;
    if (vkCreateSampler(ctx_.device, &si, nullptr, &linearClamp_) != VK_SUCCESS) { error = "vkCreateSampler failed"; return false; }
    si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    if (vkCreateSampler(ctx_.device, &si, nullptr, &linearRepeat_) != VK_SUCCESS) { error = "vkCreateSampler failed"; return false; }
    // trilinear, for the mipmapped HUD icon atlas
    si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    si.maxLod = VK_LOD_CLAMP_NONE;
    if (vkCreateSampler(ctx_.device, &si, nullptr, &linearMip_) != VK_SUCCESS) { error = "vkCreateSampler failed"; return false; }
    {   // constant procedural patterns: computed once here instead of per pixel every frame
        constexpr int kPuddle = 512;
        const std::vector<uint8_t> mask = bakePuddleMask(kPuddle);
        if (!puddleMask_.create(ctx_.device, ctx_.allocator, {kPuddle, kPuddle}, VK_FORMAT_R8_UNORM,
                                VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_IMAGE_ASPECT_COLOR_BIT, false) ||
            !puddleMask_.upload(ctx_, mask.data(), mask.size())) { error = "Pattern texture upload failed"; return false; }
    }

    const VkDescriptorPoolSize sizes[] = {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, kFramesInFlight}, {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 7 + kFramesInFlight}};
    VkDescriptorPoolCreateInfo pci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pci.maxSets = kFramesInFlight + 6;
    pci.poolSizeCount = 2;
    pci.pPoolSizes = sizes;
    if (vkCreateDescriptorPool(ctx_.device, &pci, nullptr, &pool_) != VK_SUCCESS) { error = "vkCreateDescriptorPool failed"; return false; }
    VkDescriptorSetLayout layouts[] = {pipes_.compositeSetLayout, pipes_.bloomSetLayout, pipes_.bloomSetLayout, pipes_.bloomSetLayout,
                                       pipes_.bloomSetLayout, pipes_.bloomSetLayout};
    VkDescriptorSet sets[6];
    VkDescriptorSetAllocateInfo dai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    dai.descriptorPool = pool_;
    dai.descriptorSetCount = 6;
    dai.pSetLayouts = layouts;
    if (vkAllocateDescriptorSets(ctx_.device, &dai, sets) != VK_SUCCESS) { error = "vkAllocateDescriptorSets failed"; return false; }
    compositeSet_ = sets[0]; prefilterSet_ = sets[1]; blurHSet_ = sets[2]; blurVSet_ = sets[3]; textSet_ = sets[4]; imageSet_ = sets[5];

    if (!createFrames()) { error = "Per-frame resource creation failed"; return false; }
    if (!uploadMeshes()) { error = "Mesh upload failed"; return false; }
    if (!createTargets()) { error = "Render target creation failed"; return false; }
    return true;
}

bool Renderer::createFrames() {
    timestampPeriodNs_ = ctx_.properties.limits.timestampPeriod;
    for (Frame& f : frames_) {
        if (ctx_.properties.limits.timestampComputeAndGraphics) {
            VkQueryPoolCreateInfo qi{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
            qi.queryType = VK_QUERY_TYPE_TIMESTAMP;
            qi.queryCount = 4;
            vkCreateQueryPool(ctx_.device, &qi, nullptr, &f.timestamps);
        }
        VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        pci.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        pci.queueFamilyIndex = ctx_.queueFamily;
        VK_TRY(vkCreateCommandPool(ctx_.device, &pci, nullptr, &f.pool));
        VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        ai.commandPool = f.pool;
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = 1;
        VK_TRY(vkAllocateCommandBuffers(ctx_.device, &ai, &f.cmd));
        VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        VK_TRY(vkCreateFence(ctx_.device, &fci, nullptr, &f.inFlight));
        VkSemaphoreCreateInfo sci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VK_TRY(vkCreateSemaphore(ctx_.device, &sci, nullptr, &f.imageAvailable));
        if (!f.ubo.createMapped(ctx_.allocator, sizeof(GpuFrame), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT)) return false;
        if (!f.instances.createMapped(ctx_.allocator, kMaxInstances * sizeof(Instance), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)) return false;
        if (!f.particles.createMapped(ctx_.allocator, kMaxParticles * sizeof(Particle), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)) return false;

        VkDescriptorSetAllocateInfo dai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        dai.descriptorPool = pool_;
        dai.descriptorSetCount = 1;
        dai.pSetLayouts = &pipes_.frameSetLayout;
        VK_TRY(vkAllocateDescriptorSets(ctx_.device, &dai, &f.frameSet));
        VkDescriptorBufferInfo bi{f.ubo.handle(), 0, sizeof(GpuFrame)};
        VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        w.dstSet = f.frameSet;
        w.descriptorCount = 1;
        w.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        w.pBufferInfo = &bi;
        VkDescriptorImageInfo pi{linearRepeat_, puddleMask_.view(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet wp{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        wp.dstSet = f.frameSet; wp.dstBinding = 1; wp.descriptorCount = 1;
        wp.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; wp.pImageInfo = &pi;
        const VkWriteDescriptorSet both[2] = {w, wp};
        vkUpdateDescriptorSets(ctx_.device, 2, both, 0, nullptr);
    }
    return true;
}

bool Renderer::uploadMeshes() {
    const std::vector<MeshData> lib = buildMeshLibrary();
    std::vector<Vertex> vertices;
    std::vector<uint16_t> indices;
    // Vertex offsets are baked into the indices so every draw uses vertexOffset = 0: Metal GPU families
    // without base-vertex support (iOS Simulator, Apple2) reject non-zero offsets under MoltenVK.
    for (size_t i = 0; i < lib.size(); ++i) {
        const size_t base = vertices.size();
        if (base + lib[i].vertices.size() > 0xFFFF) { CS_LOGE("Mesh library exceeds 16-bit indices"); return false; }
        meshes_[i] = {static_cast<uint32_t>(indices.size()), static_cast<uint32_t>(lib[i].indices.size()), 0};
        if (i == size_t(MeshId::HenBody)) heroBase_ = static_cast<uint32_t>(base);
        if (i >= size_t(MeshId::HenBody) && i <= size_t(MeshId::HenBeak)) heroCount_ += static_cast<uint32_t>(lib[i].vertices.size());
        vertices.insert(vertices.end(), lib[i].vertices.begin(), lib[i].vertices.end());
        for (uint16_t idx : lib[i].indices) indices.push_back(static_cast<uint16_t>(idx + base));
    }
    return vertexBuffer_.createStatic(ctx_, vertices.data(), vertices.size() * sizeof(Vertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT) &&
           indexBuffer_.createStatic(ctx_, indices.data(), indices.size() * sizeof(uint16_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
}

bool Renderer::createTargets() {
    const VkExtent2D logical = sceneExtent();
    const VkExtent2D quarter{std::max(1u, logical.width / 4), std::max(1u, logical.height / 4)};
    VkDevice dev = ctx_.device;
    const VkImageUsageFlags sampledTarget = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    if (!hdr_.create(dev, ctx_.allocator, logical, hdrFormat_, sampledTarget, VK_IMAGE_ASPECT_COLOR_BIT, false)) return false;
    if (!depth_.create(dev, ctx_.allocator, logical, ctx_.depthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_ASPECT_DEPTH_BIT, true)) return false;
    if (!bloomA_.create(dev, ctx_.allocator, quarter, hdrFormat_, sampledTarget, VK_IMAGE_ASPECT_COLOR_BIT, false)) return false;
    if (!bloomB_.create(dev, ctx_.allocator, quarter, hdrFormat_, sampledTarget, VK_IMAGE_ASPECT_COLOR_BIT, false)) return false;
    sceneFb_ = makeFramebuffer(dev, pipes_.scenePass, {hdr_.view(), depth_.view()}, logical);
    bloomAFb_ = makeFramebuffer(dev, pipes_.bloomPass, {bloomA_.view()}, quarter);
    bloomBFb_ = makeFramebuffer(dev, pipes_.bloomPass, {bloomB_.view()}, quarter);
    for (uint32_t i = 0; i < swapchain_.imageCount(); ++i)
        presentFbs_.push_back(makeFramebuffer(dev, pipes_.presentPass, {swapchain_.view(i)}, swapchain_.extent()));
    writeTargetDescriptors();
    return sceneFb_ && bloomAFb_ && bloomBFb_;
}

void Renderer::writeTargetDescriptors() {
    VkDescriptorImageInfo hdr{linearClamp_, hdr_.view(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkDescriptorImageInfo a{linearClamp_, bloomA_.view(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkDescriptorImageInfo b{linearClamp_, bloomB_.view(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    auto write = [](VkDescriptorSet set, uint32_t binding, const VkDescriptorImageInfo* info) {
        VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        w.dstSet = set; w.dstBinding = binding; w.descriptorCount = 1;
        w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; w.pImageInfo = info;
        return w;
    };
    const VkWriteDescriptorSet writes[] = {write(compositeSet_, 0, &hdr), write(compositeSet_, 1, &a), write(prefilterSet_, 0, &hdr),
                                           write(blurHSet_, 0, &a), write(blurVSet_, 0, &b)};
    vkUpdateDescriptorSets(ctx_.device, 5, writes, 0, nullptr);
}

void Renderer::destroyTargets() {
    VkDevice dev = ctx_.device;
    for (VkFramebuffer* fb : {&sceneFb_, &bloomAFb_, &bloomBFb_}) { if (*fb) vkDestroyFramebuffer(dev, *fb, nullptr); *fb = VK_NULL_HANDLE; }
    for (VkFramebuffer fb : presentFbs_) vkDestroyFramebuffer(dev, fb, nullptr);
    presentFbs_.clear();
    hdr_.destroy(); depth_.destroy(); bloomA_.destroy(); bloomB_.destroy();
}

bool Renderer::recreateSwapchain() {
    vkDeviceWaitIdle(ctx_.device);
    destroyTargets();
    if (!swapchain_.create(ctx_, platform_->drawableExtent())) return false; // e.g. zero-sized while minimised
    if (swapchain_.format() != presentFormat_) {
        presentFormat_ = swapchain_.format();
        if (!pipes_.recreatePresent(presentFormat_)) return false;
    }
    resizePending_ = false;
    return createTargets();
}

void Renderer::onSurfaceLost() {
    if (!ctx_.device) return;
    stopRecording();
    vkDeviceWaitIdle(ctx_.device);
    destroyTargets();
    swapchain_.destroy();
    ctx_.destroySurface();
}

bool Renderer::onSurfaceCreated(PlatformSurface& platform) {
    platform_ = &platform;
    if (!ctx_.createSurface(platform)) return false;
    return recreateSwapchain();
}

bool Renderer::render(const RenderList& list) {
    if (!swapchain_.valid() || !sceneFb_) {
        if (!ctx_.surface || !recreateSwapchain()) return true; // nothing to draw into yet
    }
    if (resizePending_ && !recreateSwapchain()) return true;

    if (list.fontAtlas && list.fontAtlas != uploadedAtlas_ && list.fontAtlas->built() && !uploadFontAtlas(*list.fontAtlas))
        CS_LOGE("Font atlas upload failed; text disabled");
    if (list.hudImage && list.hudImage != uploadedHudImage_ && list.hudImage->built() && !uploadHudImage(*list.hudImage))
        CS_LOGE("HUD sprite upload failed; sprites disabled");

    Frame& f = frames_[frameIndex_];
    vkWaitForFences(ctx_.device, 1, &f.inFlight, VK_TRUE, UINT64_MAX);
    if (f.timed) readTimestamps(f);

    uint32_t imageIndex = 0;
    VkResult r = swapchain_.acquire(f.imageAvailable, imageIndex);
    if (r == VK_ERROR_OUT_OF_DATE_KHR) { recreateSwapchain(); return true; }
    if (r == VK_ERROR_SURFACE_LOST_KHR) { onSurfaceLost(); return true; }
    if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) { CS_LOGE("vkAcquireNextImageKHR: %s", vkResultName(r)); return r != VK_ERROR_DEVICE_LOST; }

    // recorder: every other frame (30 fps), if the encoder has a free buffer right now (never wait for it)
    recIndex_ = UINT32_MAX;
    if (recChain_.valid() && (++recTick_ & 1u) == 0) {
        const VkResult rr = recChain_.acquire(recAcquired_[frameIndex_], recIndex_, 0);
        if (rr != VK_SUCCESS && rr != VK_SUBOPTIMAL_KHR) recIndex_ = UINT32_MAX;
    }

    vkResetFences(ctx_.device, 1, &f.inFlight);
    vkResetCommandPool(ctx_.device, f.pool, 0);
    record(f, imageIndex, list);

    const bool rec = recIndex_ != UINT32_MAX;
    const VkPipelineStageFlags waitStages[2] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT};
    const VkSemaphore waits[2] = {f.imageAvailable, recAcquired_[frameIndex_]};
    const VkSemaphore done = swapchain_.renderFinished(imageIndex);
    const VkSemaphore signals[2] = {done, rec ? recChain_.renderFinished(recIndex_) : VK_NULL_HANDLE};
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.waitSemaphoreCount = rec ? 2 : 1;
    si.pWaitSemaphores = waits;
    si.pWaitDstStageMask = waitStages;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &f.cmd;
    si.signalSemaphoreCount = rec ? 2 : 1;
    si.pSignalSemaphores = signals;
    r = vkQueueSubmit(ctx_.queue, 1, &si, f.inFlight);
    if (r != VK_SUCCESS) { CS_LOGE("vkQueueSubmit: %s", vkResultName(r)); return r != VK_ERROR_DEVICE_LOST; }
    if (rec) {
        const VkResult pr = recChain_.present(signals[1], recIndex_);
        if (pr != VK_SUCCESS && pr != VK_SUBOPTIMAL_KHR) { CS_LOGW("Recorder present: %s; recording stopped", vkResultName(pr)); stopRecording(); }
    }

    r = swapchain_.present(done, imageIndex);
    // SUBOPTIMAL on Android means the display rotated: rebuild with the new pre-transform
    if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR) resizePending_ = true;
    else if (r == VK_ERROR_SURFACE_LOST_KHR) onSurfaceLost();
    else if (r != VK_SUCCESS) { CS_LOGE("vkQueuePresentKHR: %s", vkResultName(r)); return r != VK_ERROR_DEVICE_LOST; }

    if (captureRecorded_) writeCapture();
    frameIndex_ = (frameIndex_ + 1) % kFramesInFlight;
    return true;
}

bool Renderer::uploadFontAtlas(const FontAtlas& atlas) {
    vkDeviceWaitIdle(ctx_.device);
    const VkExtent2D e{static_cast<uint32_t>(atlas.width()), static_cast<uint32_t>(atlas.height())};
    if (!fontAtlas_.create(ctx_.device, ctx_.allocator, e, VK_FORMAT_R8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                           VK_IMAGE_ASPECT_COLOR_BIT, false))
        return false;
    if (!fontAtlas_.upload(ctx_, atlas.pixels().data(), atlas.pixels().size())) return false;
    VkDescriptorImageInfo info{linearClamp_, fontAtlas_.view(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    w.dstSet = textSet_; w.dstBinding = 0; w.descriptorCount = 1;
    w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; w.pImageInfo = &info;
    vkUpdateDescriptorSets(ctx_.device, 1, &w, 0, nullptr);
    uploadedAtlas_ = &atlas;
    return true;
}

bool Renderer::uploadHudImage(const HudImage& image) {
    vkDeviceWaitIdle(ctx_.device);
    const VkExtent2D e{static_cast<uint32_t>(image.width()), static_cast<uint32_t>(image.height())};
    const uint32_t mips = 1 + static_cast<uint32_t>(image.mips().size());
    if (!hudImage_.create(ctx_.device, ctx_.allocator, e, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                          VK_IMAGE_ASPECT_COLOR_BIT, false, mips))
        return false;
    std::vector<VulkanImage::Level> levels{{image.pixels().data(), image.pixels().size()}};
    for (const auto& m : image.mips()) levels.push_back({m.data(), m.size()});
    if (!hudImage_.uploadMips(ctx_, levels.data(), mips)) return false;
    VkDescriptorImageInfo info{linearMip_, hudImage_.view(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    w.dstSet = imageSet_; w.dstBinding = 0; w.descriptorCount = 1;
    w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; w.pImageInfo = &info;
    vkUpdateDescriptorSets(ctx_.device, 1, &w, 0, nullptr);
    uploadedHudImage_ = &image;
    return true;
}

void Renderer::record(Frame& f, uint32_t imageIndex, const RenderList& list) {
    const GpuFrame gpu = toGpu(list.frame);
    f.ubo.write(&gpu, sizeof gpu);
    f.ubo.flush(0, sizeof gpu);

    // stream every instance batch into this frame's buffer: [pass][mesh] ranges, then HUD
    struct Batch { MeshId mesh; uint32_t first, count; };
    std::array<std::vector<Batch>, kPassCount> batches;
    auto* inst = static_cast<Instance*>(f.instances.mapped());
    uint32_t used = 0;
    auto push = [&](const std::vector<Instance>& src) -> std::pair<uint32_t, uint32_t> {
        const uint32_t n = static_cast<uint32_t>(std::min(src.size(), kMaxInstances - used));
        if (n < src.size() && !warnedOverflow_) { CS_LOGW("Instance buffer full; dropping draws"); warnedOverflow_ = true; }
        std::memcpy(inst + used, src.data(), n * sizeof(Instance));
        const uint32_t first = used;
        used += n;
        return {first, n};
    };
    for (size_t p = 0; p < kPassCount; ++p)
        for (size_t m = 0; m < kMeshCount; ++m) {
            const auto& src = list.buckets[p][m];
            if (src.empty()) continue;
            const auto [first, n] = push(src);
            if (n) batches[p].push_back({static_cast<MeshId>(m), first, n});
        }
    const auto [hudFirst, hudCount] = push(list.hud);
    const auto [worldTextFirst, worldTextCount] = push(list.worldText);
    const bool textReady = uploadedAtlas_ != nullptr;
    f.instances.flush(0, VkDeviceSize(used) * sizeof(Instance));
    // the posed hen: indices already carry the library's vertex offsets (no base-vertex on some Metal GPUs), so it is
    // written at the same offsets into a per-frame buffer as long as the library up to its end (made on first use)
    if (heroCount_ && !f.hero.mapped() &&
        !f.hero.createMapped(ctx_.allocator, VkDeviceSize(heroBase_ + heroCount_) * sizeof(Vertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT))
        heroCount_ = 0; // out of memory: the hen keeps its rest pose
    const bool heroPosed = heroCount_ && f.hero.mapped() && list.heroVerts.size() == heroCount_;
    if (heroPosed) {
        const VkDeviceSize off = VkDeviceSize(heroBase_) * sizeof(Vertex), bytes = VkDeviceSize(heroCount_) * sizeof(Vertex);
        std::memcpy(static_cast<uint8_t*>(f.hero.mapped()) + off, list.heroVerts.data(), size_t(bytes));
        f.hero.flush(off, bytes);
    }

    auto* parts = static_cast<Particle*>(f.particles.mapped());
    const uint32_t smokeCount = static_cast<uint32_t>(std::min(list.particlesSmoke.size(), kMaxParticles));
    const uint32_t addCount = static_cast<uint32_t>(std::min(list.particlesAdd.size(), kMaxParticles - smokeCount));
    std::memcpy(parts, list.particlesSmoke.data(), smokeCount * sizeof(Particle));
    std::memcpy(parts + smokeCount, list.particlesAdd.data(), addCount * sizeof(Particle));
    f.particles.flush(0, VkDeviceSize(smokeCount + addCount) * sizeof(Particle));

    VkCommandBuffer cmd = f.cmd;
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);
    f.timed = gpuTiming_ && f.timestamps;
    if (f.timed) {
        vkCmdResetQueryPool(cmd, f.timestamps, 0, 4);
        vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, f.timestamps, 0);
    }

    const VkExtent2D logical = sceneExtent(); // the 3D scene's internal resolution (render scale)
    const glm::mat4 viewProj = list.frame.proj * list.frame.view;

    // ---- scene pass: HDR colour, transient depth ----
    {
        VkClearValue clears[2];
        clears[0].color = {{0, 0, 0, 1}};
        clears[1].depthStencil = {1.f, 0};
        VkRenderPassBeginInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        rp.renderPass = pipes_.scenePass;
        rp.framebuffer = sceneFb_;
        rp.renderArea = {{0, 0}, logical};
        rp.clearValueCount = 2;
        rp.pClearValues = clears;
        vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);
        setViewport(cmd, logical);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipes_.sceneLayout, 0, 1, &f.frameSet, 0, nullptr);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipes_.sky);
        vkCmdDraw(cmd, 3, 1, 0, 0);

        const VkDeviceSize zero = 0;
        VkBuffer vb = vertexBuffer_.handle();
        vkCmdBindVertexBuffers(cmd, 0, 1, &vb, &zero);
        vkCmdBindIndexBuffer(cmd, indexBuffer_.handle(), 0, VK_INDEX_TYPE_UINT16);
        VkBuffer ib = f.instances.handle();

        ScenePush push{};
        std::memcpy(push.viewProj, &viewProj[0][0], sizeof push.viewProj);
        auto drawPass = [&](Pass p, VkPipeline pipeline) {
            if (batches[size_t(p)].empty()) return;
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
            vkCmdPushConstants(cmd, pipes_.sceneLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof push, &push);
            for (const Batch& b : batches[size_t(p)]) {
                const MeshRange& m = meshes_[size_t(b.mesh)];
                const VkDeviceSize off = VkDeviceSize(b.first) * sizeof(Instance);
                const bool hero = heroPosed && b.mesh >= MeshId::HenBody && b.mesh <= MeshId::HenBeak;
                if (hero) { VkBuffer hb = f.hero.handle(); vkCmdBindVertexBuffers(cmd, 0, 1, &hb, &zero); }
                vkCmdBindVertexBuffers(cmd, 1, 1, &ib, &off);
                vkCmdDrawIndexed(cmd, m.indexCount, b.count, m.firstIndex, m.vertexOffset, 0);
                if (hero) vkCmdBindVertexBuffers(cmd, 0, 1, &vb, &zero);
            }
        };
        drawPass(Pass::Lit, pipes_.lit);
        drawPass(Pass::LitTwoSided, pipes_.litTwoSided);
        drawPass(Pass::UnlitAlpha, pipes_.unlitAlpha);

        if (worldTextCount && textReady) { // neon sign words, today's-best gate label
            const MeshRange& plane = meshes_[size_t(MeshId::Plane)];
            const VkDeviceSize off = VkDeviceSize(worldTextFirst) * sizeof(Instance);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipes_.textWorld);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipes_.textLayout, 0, 1, &textSet_, 0, nullptr);
            vkCmdPushConstants(cmd, pipes_.textLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof push, &push);
            vkCmdBindVertexBuffers(cmd, 1, 1, &ib, &off);
            vkCmdDrawIndexed(cmd, plane.indexCount, worldTextCount, plane.firstIndex, plane.vertexOffset, 0);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipes_.sceneLayout, 0, 1, &f.frameSet, 0, nullptr);
        }

        VkBuffer pb = f.particles.handle();
        auto drawParticles = [&](VkPipeline pipeline, uint32_t first, uint32_t count, float kind) {
            if (!count) return;
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
            push.kind[0] = kind;
            vkCmdPushConstants(cmd, pipes_.sceneLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof push, &push);
            const VkDeviceSize off = VkDeviceSize(first) * sizeof(Particle);
            vkCmdBindVertexBuffers(cmd, 0, 1, &pb, &off);
            vkCmdDraw(cmd, 4, count, 0, 0);
        };
        drawParticles(pipes_.particleAlpha, 0, smokeCount, 1.f);
        push.kind[0] = 0.f;
        vkCmdBindVertexBuffers(cmd, 0, 1, &vb, &zero);
        drawPass(Pass::UnlitAdd, pipes_.unlitAdd);
        drawParticles(pipes_.particleAdd, smokeCount, addCount, 0.f);
        vkCmdEndRenderPass(cmd);
    }

    if (f.timed) vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, f.timestamps, 1);
    // ---- bloom: prefilter to 1/4 res, separable blur ----
    {
        const VkExtent2D q = bloomA_.extent();
        auto bloomPass = [&](VkFramebuffer fb, VkDescriptorSet src, PostPush pp) {
            VkClearValue clear{};
            VkRenderPassBeginInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
            rp.renderPass = pipes_.bloomPass;
            rp.framebuffer = fb;
            rp.renderArea = {{0, 0}, q};
            rp.clearValueCount = 1;
            rp.pClearValues = &clear;
            vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);
            setViewport(cmd, q);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipes_.bloom);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipes_.bloomLayout, 0, 1, &src, 0, nullptr);
            vkCmdPushConstants(cmd, pipes_.bloomLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof pp, &pp);
            vkCmdDraw(cmd, 3, 1, 0, 0);
            vkCmdEndRenderPass(cmd);
        };
        const float tx = 1.f / q.width, ty = 1.f / q.height;
        // UnrealBloomPass(threshold 1.15) in the web build; soft knee keeps neon edges from popping
        bloomPass(bloomAFb_, prefilterSet_, {{1, 0, 0, 1}, {1.f / logical.width, 1.f / logical.height, 0, 0}, {0, 1.15f, 0.5f, 0}});
        bloomPass(bloomBFb_, blurHSet_, {{1, 0, 0, 1}, {tx, ty, 1, 0}, {1, 0, 0, 0}});
        bloomPass(bloomAFb_, blurVSet_, {{1, 0, 0, 1}, {tx, ty, 0, 1}, {1, 0, 0, 0}});
    }

    if (f.timed) vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, f.timestamps, 2);
    // ---- present pass: composite (tone map) + HUD, pre-rotated for the surface transform ----
    {
        VkClearValue clear{};
        clear.color = {{0, 0, 0, 1}};
        VkRenderPassBeginInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        rp.renderPass = pipes_.presentPass;
        rp.framebuffer = presentFbs_[imageIndex];
        rp.renderArea = {{0, 0}, swapchain_.extent()};
        rp.clearValueCount = 1;
        rp.pClearValues = &clear;
        vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);
        setViewport(cmd, swapchain_.extent());

        const glm::mat2 rot = swapchain_.preRotation();
        const FrameParams& fp = list.frame;
        // frosted-glass rect, HUD points -> uv (x in uv, distances in units of the screen height)
        const float vw = std::max(fp.viewportW, 1.f), vh = std::max(fp.viewportH, 1.f);
        PostPush pp{{rot[0][0], rot[0][1], rot[1][0], rot[1][1]}, {fp.chromaAmount, fp.vignette, fp.bloomStrength, swapchain_.isSrgb() ? 0.f : 1.f},
                    {fp.vignetteTint.r, fp.vignetteTint.g, fp.vignetteTint.b, fp.vignetteTint.a},
                    {fp.frostRect.x / vw, fp.frostRect.y / vh, fp.frostRect.z / 2 / vh, fp.frostRect.w / 2 / vh},
                    {fp.frostRadius / vh, vw / vh, fp.frostAmount, 18.f / vh}};
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipes_.composite);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipes_.compositeLayout, 0, 1, &compositeSet_, 0, nullptr);
        vkCmdPushConstants(cmd, pipes_.compositeLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof pp, &pp);
        vkCmdDraw(cmd, 3, 1, 0, 0);

        if (hudCount) {
            // HUD space: logical points, origin top-left (Vulkan clip Y already points down)
            glm::mat4 rot4(1.f);
            rot4[0] = glm::vec4(rot[0], 0, 0);
            rot4[1] = glm::vec4(rot[1], 0, 0);
            const glm::mat4 hudMat = rot4 * glm::orthoZO(0.f, fp.viewportW, 0.f, fp.viewportH, -1.f, 1.f);
            ScenePush hp{};
            std::memcpy(hp.viewProj, &hudMat[0][0], sizeof hp.viewProj);
            const VkBuffer vb = vertexBuffer_.handle();
            const VkDeviceSize zero = 0;
            const VkBuffer ib = f.instances.handle();
            vkCmdBindVertexBuffers(cmd, 0, 1, &vb, &zero);
            vkCmdBindIndexBuffer(cmd, indexBuffer_.handle(), 0, VK_INDEX_TYPE_UINT16);
            const MeshRange& plane = meshes_[size_t(MeshId::Plane)];
            // paint order matters (flash under the HUD, overlay text above its backdrop): draw runs of quads / glyphs
            uint32_t start = 0;
            int bound = -1;
            const auto kindAt = [&](uint32_t i) { return i < list.hudKind.size() ? int(list.hudKind[i]) : int(HudShape); };
            const bool spritesReady = uploadedHudImage_ != nullptr;
            while (start < hudCount) {
                const int kind = kindAt(start);
                uint32_t end = start + 1;
                while (end < hudCount && kindAt(end) == kind) ++end;
                const bool ready = kind == HudShape || (kind == HudText && textReady) || (kind == HudSprite && spritesReady);
                if (ready) {
                    if (bound != kind) {
                        bound = kind;
                        if (kind == HudText || kind == HudSprite) {
                            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, kind == HudText ? pipes_.textHud : pipes_.hudImage);
                            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipes_.textLayout, 0, 1, kind == HudText ? &textSet_ : &imageSet_, 0, nullptr);
                            vkCmdPushConstants(cmd, pipes_.textLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof hp, &hp);
                        } else {
                            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipes_.hud);
                            vkCmdPushConstants(cmd, pipes_.sceneLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof hp, &hp);
                        }
                    }
                    const VkDeviceSize off = VkDeviceSize(hudFirst + start) * sizeof(Instance);
                    vkCmdBindVertexBuffers(cmd, 1, 1, &ib, &off);
                    vkCmdDrawIndexed(cmd, plane.indexCount, end - start, plane.firstIndex, plane.vertexOffset, 0);
                }
                start = end;
            }
        }
        vkCmdEndRenderPass(cmd);
    }
    if (f.timed) vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, f.timestamps, 3);
    if (!capturePath_.empty()) recordCapture(cmd, imageIndex);
    if (recIndex_ != UINT32_MAX) recordBlit(cmd, imageIndex);
    vkEndCommandBuffer(cmd);
}

void Renderer::recordCapture(VkCommandBuffer cmd, uint32_t imageIndex) {
    if (!swapchain_.canReadback()) { CS_LOGW("Swapchain does not support readback; capture skipped"); capturePath_.clear(); return; }
    const VkExtent2D e = swapchain_.extent();
    const VkDeviceSize bytes = VkDeviceSize(e.width) * e.height * 4;
    if (captureBuffer_.size() < bytes && !captureBuffer_.createMapped(ctx_.allocator, bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT)) { capturePath_.clear(); return; }
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    b.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    b.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = swapchain_.image(imageIndex);
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
    VkBufferImageCopy copy{};
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageExtent = {e.width, e.height, 1};
    vkCmdCopyImageToBuffer(cmd, b.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, captureBuffer_.handle(), 1, &copy);
    b.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    b.dstAccessMask = 0;
    b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    b.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
    VkMemoryBarrier host{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    host.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    host.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &host, 0, nullptr, 0, nullptr);
    captureRecorded_ = true;
}

// ---------------------------------------------------------------- video recording

bool Renderer::startRecording(PlatformSurface& encoderSurface, VkExtent2D size) {
    stopRecording();
    if (!swapchain_.valid() || !swapchain_.canReadback()) { CS_LOGW("Recorder: swapchain can't be read back"); return false; }
    if (swapchain_.preRotation() != glm::mat2(1.f)) { CS_LOGW("Recorder: rotated display, not recording"); return false; }
    VkFormatProperties fp{};
    vkGetPhysicalDeviceFormatProperties(ctx_.gpu, swapchain_.format(), &fp);
    if (!(fp.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT)) { CS_LOGW("Recorder: screen format can't be blitted"); return false; }
    if (encoderSurface.createSurface(ctx_.instance, &recSurface_) != VK_SUCCESS) { recSurface_ = VK_NULL_HANDLE; return false; }
    VkBool32 present = VK_FALSE;
    vkGetPhysicalDeviceSurfaceSupportKHR(ctx_.gpu, ctx_.queueFamily, recSurface_, &present);
    if (!present || !recChain_.create(ctx_, size, recSurface_, VK_IMAGE_USAGE_TRANSFER_DST_BIT)) { stopRecording(); return false; }
    vkGetPhysicalDeviceFormatProperties(ctx_.gpu, recChain_.format(), &fp);
    if (!(fp.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT)) { CS_LOGW("Recorder: encoder format can't be blitted"); stopRecording(); return false; }
    VkSemaphoreCreateInfo sci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    for (VkSemaphore& sem : recAcquired_)
        if (vkCreateSemaphore(ctx_.device, &sci, nullptr, &sem) != VK_SUCCESS) { stopRecording(); return false; }
    recTick_ = 0;
    CS_LOGI("Recorder: %ux%u, format %d", recChain_.extent().width, recChain_.extent().height, int(recChain_.format()));
    return true;
}

void Renderer::stopRecording() {
    if (!recChain_.valid() && !recSurface_) return;
    vkDeviceWaitIdle(ctx_.device);
    recChain_.destroy();
    for (VkSemaphore& sem : recAcquired_) { if (sem) vkDestroySemaphore(ctx_.device, sem, nullptr); sem = VK_NULL_HANDLE; }
    if (recSurface_) vkDestroySurfaceKHR(ctx_.instance, recSurface_, nullptr);
    recSurface_ = VK_NULL_HANDLE;
    recIndex_ = UINT32_MAX;
    recOverlay_.destroy();
    recOverlayOn_ = false;
}

bool Renderer::setRecordOverlay(const uint8_t* rgba, uint32_t width, uint32_t height) {
    vkDeviceWaitIdle(ctx_.device);
    if (!recOverlay_.create(ctx_.device, ctx_.allocator, {width, height}, VK_FORMAT_R8G8B8A8_UNORM,
                            VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VK_IMAGE_ASPECT_COLOR_BIT, false) ||
        !recOverlay_.upload(ctx_, rgba, size_t(width) * height * 4))
        return false;
    recOverlayOn_ = true;
    return true;
}

void Renderer::recordBlit(VkCommandBuffer cmd, uint32_t imageIndex) {
    if (recOverlayOn_) { // the end card instead of the screen
        const VkImage src = recOverlay_.image(), dst = recChain_.image(recIndex_);
        VkImageMemoryBarrier b[2]{};
        for (VkImageMemoryBarrier& x : b) {
            x.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            x.srcQueueFamilyIndex = x.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            x.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        }
        b[0].image = src; b[0].oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; b[0].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        b[0].srcAccessMask = 0; b[0].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        b[1].image = dst; b[1].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; b[1].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        b[1].srcAccessMask = 0; b[1].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 2, b);
        const VkExtent2D se = recOverlay_.extent(), de = recChain_.extent();
        VkImageBlit blit{};
        blit.srcSubresource = blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        blit.srcOffsets[1] = {int32_t(se.width), int32_t(se.height), 1};
        blit.dstOffsets[1] = {int32_t(de.width), int32_t(de.height), 1};
        vkCmdBlitImage(cmd, src, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
        b[0].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL; b[0].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        b[0].srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT; b[0].dstAccessMask = 0;
        b[1].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; b[1].newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        b[1].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; b[1].dstAccessMask = 0;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 2, b);
        return;
    }
    const VkImage src = swapchain_.image(imageIndex), dst = recChain_.image(recIndex_);
    VkImageMemoryBarrier b[2]{};
    for (VkImageMemoryBarrier& x : b) {
        x.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        x.srcQueueFamilyIndex = x.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        x.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    }
    b[0].image = src; b[0].oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR; b[0].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    b[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT; b[0].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    b[1].image = dst; b[1].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; b[1].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    b[1].srcAccessMask = 0; b[1].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 2, b);
    const VkExtent2D se = swapchain_.extent(), de = recChain_.extent();
    VkImageBlit blit{};
    blit.srcSubresource = blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blit.srcOffsets[1] = {int32_t(se.width), int32_t(se.height), 1};
    blit.dstOffsets[1] = {int32_t(de.width), int32_t(de.height), 1};
    vkCmdBlitImage(cmd, src, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
    b[0].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL; b[0].newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    b[0].srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT; b[0].dstAccessMask = 0;
    b[1].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; b[1].newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    b[1].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; b[1].dstAccessMask = 0;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 2, b);
}

void Renderer::writeCapture() {
    vkDeviceWaitIdle(ctx_.device);
    captureBuffer_.invalidate(); // needed on non-coherent host memory before the CPU reads
    const VkExtent2D e = swapchain_.extent();
    const auto* px = static_cast<const uint8_t*>(captureBuffer_.mapped());
    const bool bgr = swapchain_.format() == VK_FORMAT_B8G8R8A8_UNORM || swapchain_.format() == VK_FORMAT_B8G8R8A8_SRGB;
    if (FILE* f = std::fopen(capturePath_.c_str(), "wb")) {
        std::fprintf(f, "P6\n%u %u\n255\n", e.width, e.height);
        std::vector<uint8_t> row(size_t(e.width) * 3);
        for (uint32_t y = 0; y < e.height; ++y) {
            for (uint32_t x = 0; x < e.width; ++x) {
                const uint8_t* p = px + (size_t(y) * e.width + x) * 4;
                row[x * 3 + 0] = bgr ? p[2] : p[0];
                row[x * 3 + 1] = p[1];
                row[x * 3 + 2] = bgr ? p[0] : p[2];
            }
            std::fwrite(row.data(), 1, row.size(), f);
        }
        std::fclose(f);
        CS_LOGI("Captured frame to %s", capturePath_.c_str());
    }
    capturePath_.clear();
    captureRecorded_ = false;
}

VkExtent2D Renderer::sceneExtent() const {
    const VkExtent2D e = swapchain_.logicalExtent();
    return {std::max(1u, uint32_t(std::lround(e.width * renderScale_))), std::max(1u, uint32_t(std::lround(e.height * renderScale_)))};
}

void Renderer::setRenderScale(float scale) {
    scale = std::clamp(scale, 0.4f, 1.f);
    if (std::abs(scale - renderScale_) < 0.01f) return;
    renderScale_ = scale;
    resizePending_ = true; // rebuild the HDR / depth / bloom targets at the next frame
    CS_LOGI("Render scale %.2f", scale);
}

void Renderer::readTimestamps(Frame& f) {
    uint64_t t[4];
    if (vkGetQueryPoolResults(ctx_.device, f.timestamps, 0, 4, sizeof t, t, sizeof(uint64_t), VK_QUERY_RESULT_64_BIT) != VK_SUCCESS) return;
    for (int i = 0; i < 3; ++i) gpuAccum_[i] += double(t[i + 1] - t[i]) * timestampPeriodNs_ * 1e-6;
    if (++gpuFrames_ >= 120) {
        CS_LOGI("GPU ms/frame: scene %.2f  bloom %.2f  present %.2f  total %.2f  (%ux%u)", gpuAccum_[0] / gpuFrames_, gpuAccum_[1] / gpuFrames_,
                gpuAccum_[2] / gpuFrames_, (gpuAccum_[0] + gpuAccum_[1] + gpuAccum_[2]) / gpuFrames_, hdr_.extent().width, hdr_.extent().height);
        gpuAccum_[0] = gpuAccum_[1] = gpuAccum_[2] = 0; gpuFrames_ = 0;
    }
}

void Renderer::shutdown() {
    if (!ctx_.device) return;
    vkDeviceWaitIdle(ctx_.device);
    destroyTargets();
    for (Frame& f : frames_) {
        f.ubo.destroy(); f.instances.destroy(); f.particles.destroy(); f.hero.destroy();
        if (f.inFlight) vkDestroyFence(ctx_.device, f.inFlight, nullptr);
        if (f.imageAvailable) vkDestroySemaphore(ctx_.device, f.imageAvailable, nullptr);
        if (f.pool) vkDestroyCommandPool(ctx_.device, f.pool, nullptr);
        if (f.timestamps) vkDestroyQueryPool(ctx_.device, f.timestamps, nullptr);
        f = Frame{};
    }
    stopRecording();
    vertexBuffer_.destroy(); indexBuffer_.destroy(); captureBuffer_.destroy(); fontAtlas_.destroy(); puddleMask_.destroy(); hudImage_.destroy();
    if (linearRepeat_) vkDestroySampler(ctx_.device, linearRepeat_, nullptr);
    linearRepeat_ = VK_NULL_HANDLE;
    if (linearClamp_) vkDestroySampler(ctx_.device, linearClamp_, nullptr);
    if (linearMip_) vkDestroySampler(ctx_.device, linearMip_, nullptr);
    if (pool_) vkDestroyDescriptorPool(ctx_.device, pool_, nullptr);
    linearClamp_ = linearMip_ = VK_NULL_HANDLE; pool_ = VK_NULL_HANDLE;
    pipes_.destroy();
    swapchain_.destroy();
    ctx_.shutdown();
}

} // namespace cs
