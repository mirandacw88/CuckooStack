// Shared Vulkan includes for the renderer. volk provides every entry point (VK_NO_PROTOTYPES), so nothing
// links libvulkan/MoltenVK symbols directly except the iOS loader bootstrap. No OS headers allowed here.
#pragma once

#include <volk.h>
#include <vk_mem_alloc.h>

#include "../core/Log.h"

namespace cs {

const char* vkResultName(VkResult r);

} // namespace cs

// Logs and returns false from the enclosing function on failure; use in bool-returning init paths.
#define VK_TRY(expr)                                                                         \
    do {                                                                                     \
        const VkResult vkTryResult_ = (expr);                                                \
        if (vkTryResult_ != VK_SUCCESS) {                                                    \
            CS_LOGE("%s failed: %s (%s:%d)", #expr, ::cs::vkResultName(vkTryResult_), __FILE__, __LINE__); \
            return false;                                                                    \
        }                                                                                    \
    } while (0)
