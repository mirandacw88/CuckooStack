// Frame orchestration: frames-in-flight synchronisation (fences + semaphores), per-frame streaming buffers,
// render targets, and the frame graph:
//   scene pass (HDR + transient depth) -> bloom prefilter/blur (1/4 res) -> present pass (composite + HUD).
#pragma once

#include "VulkanBuffer.h"
#include "VulkanContext.h"
#include "VulkanImage.h"
#include "VulkanPipeline.h"
#include "VulkanSwapchain.h"
#include "../core/RenderList.h"

#include <array>
#include <string>
#include <vector>

namespace cs {

class PlatformSurface;

class Renderer {
public:
    static constexpr uint32_t kFramesInFlight = 2;

    bool init(PlatformSurface& platform, bool enableValidation, std::string& error);
    void shutdown();

    // Draws one frame. Returns false only on unrecoverable errors (device lost).
    bool render(const RenderList& list);

    // Window lifecycle (Android destroys the ANativeWindow whenever the app is backgrounded).
    void onSurfaceLost();
    bool onSurfaceCreated(PlatformSurface& platform);
    void requestResize() { resizePending_ = true; }
    bool hasSurface() const { return swapchain_.valid(); }

    void setGpuTiming(bool on) { gpuTiming_ = on; }
    // Where to keep the driver's compiled pipelines between launches (set before init()).
    void setPipelineCachePath(std::string path) { pipelineCachePath_ = std::move(path); }

    // Internal resolution of the 3D scene (and bloom) relative to the screen, 0.4..1. The composite pass upscales
    // it with bilinear filtering; the HUD and text always render at native resolution. Changing it rebuilds the
    // render targets at the next frame, so call it rarely (startup, thermal-state changes).
    void setRenderScale(float scale);
    float renderScale() const { return renderScale_; }

    // Debug/testing: write the next presented frame to a binary PPM (desktop golden-image checks).
    void captureNextFrame(const std::string& ppmPath) { capturePath_ = ppmPath; }

    // Video recording (Android Share Replay): a second swapchain on the encoder's input surface. Every other frame
    // the finished image (HUD included) is scaled into it on the GPU, so no pixels come back to the CPU.
    // False when unsupported (no readback usage, rotated display, formats without blit).
    bool startRecording(PlatformSurface& encoderSurface, VkExtent2D size);
    void stopRecording();
    bool recording() const { return recChain_.valid(); }
    // While set, the recorder receives this image (RGBA8, e.g. the replay end card) instead of the screen.
    bool setRecordOverlay(const uint8_t* rgba, uint32_t width, uint32_t height);
    void clearRecordOverlay() { recOverlayOn_ = false; }

    // Logical drawable size in pixels (orientation the user sees)
    VkExtent2D logicalExtent() const { return swapchain_.logicalExtent(); }

private:
    struct Frame {
        VkQueryPool timestamps = VK_NULL_HANDLE; // GPU timing: start, scene, bloom, present
        bool timed = false;
        VkCommandPool pool = VK_NULL_HANDLE;
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        VkFence inFlight = VK_NULL_HANDLE;
        VkSemaphore imageAvailable = VK_NULL_HANDLE;
        VulkanBuffer ubo, instances, particles;
        VkDescriptorSet frameSet = VK_NULL_HANDLE;
    };
    struct MeshRange { uint32_t firstIndex, indexCount; int32_t vertexOffset; };

    bool createFrames();
    bool uploadMeshes();
    bool createTargets();
    void destroyTargets();
    bool recreateSwapchain();
    void writeTargetDescriptors();
    void record(Frame& f, uint32_t imageIndex, const RenderList& list);
    bool uploadFontAtlas(const FontAtlas& atlas);
    bool uploadHudImage(const HudImage& image);

    PlatformSurface* platform_ = nullptr;
    VulkanContext ctx_;
    VulkanSwapchain swapchain_;
    VulkanPipeline pipes_;
    VkFormat hdrFormat_ = VK_FORMAT_R16G16B16A16_SFLOAT;
    VkFormat presentFormat_ = VK_FORMAT_UNDEFINED;

    std::array<Frame, kFramesInFlight> frames_{};
    uint32_t frameIndex_ = 0;

    VulkanBuffer vertexBuffer_, indexBuffer_;
    std::array<MeshRange, kMeshCount> meshes_{};

    VulkanImage hdr_, depth_, bloomA_, bloomB_;
    VulkanImage fontAtlas_;
    VulkanImage puddleMask_;              // baked asphalt puddles (BakedPatterns.cpp)
    VkSampler linearRepeat_ = VK_NULL_HANDLE;
    VkDescriptorSet textSet_ = VK_NULL_HANDLE;
    const FontAtlas* uploadedAtlas_ = nullptr;
    VulkanImage hudImage_;                 // full-colour HUD sprites (lives heart)
    VkDescriptorSet imageSet_ = VK_NULL_HANDLE;
    const HudImage* uploadedHudImage_ = nullptr;
    VkFramebuffer sceneFb_ = VK_NULL_HANDLE, bloomAFb_ = VK_NULL_HANDLE, bloomBFb_ = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> presentFbs_;
    VkSampler linearClamp_ = VK_NULL_HANDLE, linearMip_ = VK_NULL_HANDLE;
    VkDescriptorPool pool_ = VK_NULL_HANDLE;
    VkDescriptorSet compositeSet_ = VK_NULL_HANDLE, prefilterSet_ = VK_NULL_HANDLE, blurHSet_ = VK_NULL_HANDLE, blurVSet_ = VK_NULL_HANDLE;

    void recordCapture(VkCommandBuffer cmd, uint32_t imageIndex);
    void writeCapture();
    std::string capturePath_;
    VulkanBuffer captureBuffer_;
    bool captureRecorded_ = false;

    // GPU pass timing (enable with setGpuTiming): averaged ms for scene / bloom / present, logged every 2 s
    bool gpuTiming_ = false;
    double gpuAccum_[3] = {}, gpuFrames_ = 0;
    float timestampPeriodNs_ = 1.f;
    void readTimestamps(Frame& f);

    float renderScale_ = 1.f;
    std::string pipelineCachePath_;
    VkExtent2D sceneExtent() const;

    VkSurfaceKHR recSurface_ = VK_NULL_HANDLE;
    VulkanSwapchain recChain_;
    std::array<VkSemaphore, kFramesInFlight> recAcquired_{};
    uint32_t recTick_ = 0, recIndex_ = UINT32_MAX;
    VulkanImage recOverlay_;
    bool recOverlayOn_ = false;
    void recordBlit(VkCommandBuffer cmd, uint32_t imageIndex);

    bool resizePending_ = false;
    bool warnedOverflow_ = false;
};

} // namespace cs
