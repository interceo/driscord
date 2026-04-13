#include "driscord_jni_vulkan.hpp"
#include "jni_common.hpp"

#ifdef DRISCORD_HAS_VULKAN

#include "utils/log.hpp"
#include "video/vulkan_renderer.hpp"

#include <cstring>
#include <jawt.h>
#include <jawt_md.h>
#include <memory>
#include <mutex>
#include <unordered_map>

static std::mutex g_vk_mutex;
static std::unordered_map<std::string, std::unique_ptr<VulkanRenderer>> g_vk_renderers;

bool vulkan_try_present(const std::string& peer_id, const uint8_t* rgba, int w, int h)
{
    std::lock_guard<std::mutex> lock(g_vk_mutex);
    auto it = g_vk_renderers.find(peer_id);
    if (it == g_vk_renderers.end()) return false;

    size_t cap = 0;
    auto* staging = it->second->acquire_staging(w, h, cap);
    if (!staging) return false;

    std::memcpy(staging, rgba, static_cast<size_t>(w) * h * 4);
    it->second->present(w, h);
    return true;
}

void vulkan_detach_all()
{
    std::lock_guard<std::mutex> lock(g_vk_mutex);
    for (auto& [_, r] : g_vk_renderers) r->destroy();
    g_vk_renderers.clear();
}

// Extract platform native window/display handles from an AWT Component via JAWT.
static bool jawt_get_native_handles(JNIEnv* env, jobject component,
    uint64_t& out_window, uint64_t& out_display)
{
    JAWT awt {};
    awt.version = JAWT_VERSION_9;
    if (!JAWT_GetAWT(env, &awt)) {
        LOG_ERROR() << "JAWT_GetAWT failed";
        return false;
    }

    JAWT_DrawingSurface* ds = awt.GetDrawingSurface(env, component);
    if (!ds) {
        LOG_ERROR() << "JAWT GetDrawingSurface failed";
        return false;
    }

    jint lock = ds->Lock(ds);
    if (lock & JAWT_LOCK_ERROR) {
        awt.FreeDrawingSurface(ds);
        LOG_ERROR() << "JAWT Lock failed";
        return false;
    }

    JAWT_DrawingSurfaceInfo* dsi = ds->GetDrawingSurfaceInfo(ds);
    if (!dsi || !dsi->platformInfo) {
        ds->Unlock(ds);
        awt.FreeDrawingSurface(ds);
        LOG_ERROR() << "JAWT GetDrawingSurfaceInfo failed";
        return false;
    }

    bool ok = false;
#ifdef __linux__
    auto* x11 = reinterpret_cast<JAWT_X11DrawingSurfaceInfo*>(dsi->platformInfo);
    out_window = static_cast<uint64_t>(x11->drawable);
    out_display = reinterpret_cast<uint64_t>(x11->display);
    ok = true;
#elif defined(_WIN32)
    auto* win = reinterpret_cast<JAWT_Win32DrawingSurfaceInfo*>(dsi->platformInfo);
    out_window = reinterpret_cast<uint64_t>(win->hwnd);
    out_display = 0;
    ok = true;
#endif

    ds->FreeDrawingSurfaceInfo(dsi);
    ds->Unlock(ds);
    awt.FreeDrawingSurface(ds);
    return ok;
}

#else // !DRISCORD_HAS_VULKAN

bool vulkan_try_present(const std::string&, const uint8_t*, int, int) { return false; }
void vulkan_detach_all() {}

#endif // DRISCORD_HAS_VULKAN

// ---------------------------------------------------------------------------
// JNI exports
// ---------------------------------------------------------------------------

extern "C" {

JNIEXPORT jboolean JNICALL
Java_com_driscord_jni_NativeDriscord_vulkanAvailable(JNIEnv*, jclass)
{
#ifdef DRISCORD_HAS_VULKAN
    return VulkanRenderer::is_available() ? JNI_TRUE : JNI_FALSE;
#else
    return JNI_FALSE;
#endif
}

JNIEXPORT jboolean JNICALL
Java_com_driscord_jni_NativeDriscord_vulkanAttach(JNIEnv* env, jclass,
    jstring jPeerId, jobject component, jint w, jint h)
{
#ifdef DRISCORD_HAS_VULKAN
    auto peerId = jni_jstring_to_utf8(env, jPeerId);
    LOG_INFO() << "vulkanAttach: peer=" << peerId << " size=" << w << "x" << h;

    uint64_t window_handle = 0, display_handle = 0;
    if (!jawt_get_native_handles(env, component, window_handle, display_handle)) {
        LOG_ERROR() << "vulkanAttach: JAWT failed for " << peerId;
        return JNI_FALSE;
    }

    auto renderer = std::make_unique<VulkanRenderer>();
    if (!renderer->init(window_handle, display_handle, w, h)) {
        LOG_ERROR() << "vulkanAttach: VulkanRenderer::init failed for " << peerId;
        return JNI_FALSE;
    }

    LOG_INFO() << "vulkanAttach: OK for " << peerId
               << " window=0x" << std::hex << window_handle << std::dec;

    std::lock_guard<std::mutex> lock(g_vk_mutex);
    auto it = g_vk_renderers.find(peerId);
    if (it != g_vk_renderers.end()) {
        it->second->destroy();
    }
    g_vk_renderers[peerId] = std::move(renderer);
    return JNI_TRUE;
#else
    (void)env; (void)jPeerId; (void)component; (void)w; (void)h;
    return JNI_FALSE;
#endif
}

JNIEXPORT void JNICALL
Java_com_driscord_jni_NativeDriscord_vulkanDetach(JNIEnv* env, jclass,
    jstring jPeerId)
{
#ifdef DRISCORD_HAS_VULKAN
    auto peerId = jni_jstring_to_utf8(env, jPeerId);
    std::lock_guard<std::mutex> lock(g_vk_mutex);
    auto it = g_vk_renderers.find(peerId);
    if (it != g_vk_renderers.end()) {
        it->second->destroy();
        g_vk_renderers.erase(it);
    }
#else
    (void)env; (void)jPeerId;
#endif
}

JNIEXPORT void JNICALL
Java_com_driscord_jni_NativeDriscord_vulkanResize(JNIEnv* env, jclass,
    jstring jPeerId, jint w, jint h)
{
#ifdef DRISCORD_HAS_VULKAN
    auto peerId = jni_jstring_to_utf8(env, jPeerId);
    std::lock_guard<std::mutex> lock(g_vk_mutex);
    auto it = g_vk_renderers.find(peerId);
    if (it != g_vk_renderers.end()) {
        it->second->resize(w, h);
    }
#else
    (void)env; (void)jPeerId; (void)w; (void)h;
#endif
}

JNIEXPORT void JNICALL
Java_com_driscord_jni_NativeDriscord_vulkanDetachAll(JNIEnv*, jclass)
{
    vulkan_detach_all();
}

} // extern "C"
