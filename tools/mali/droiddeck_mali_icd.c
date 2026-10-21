/* SPDX-License-Identifier: MIT
 *
 * DroidDeck DRM-node adapter, version 0.2.0 (Mali-G77 session layout).
 *
 * Derived from the DroidDeck Mali-G615 experimental ICD adapter 0.1.0,
 * MIT, (c) 2026 DroidDeck Mali-G615 integration contributors - see
 * adapter-LICENSE. The changes for this device:
 *   - the device gate accepts any ARM Mali GPU, not only "Mali-G615";
 *   - the node lookup falls back to scanning /dev/dri, because DroidDeck's
 *     session does not bind /dev/mali0 at all - it binds the kbase node as
 *     /dev/dri/renderD<minor>, and only that path exists in the guest.
 *
 * This is NOT a new GPU kernel driver. Rendering and all GPU capabilities are
 * delegated to an unmodified ARM64/glibc PanVK-kbase driver beside this file.
 * The only added device extension is the DRM-node compatibility facade needed
 * by gamescope's nested backend: a driver that reaches the GPU through
 * mali_kbase reports no DRM nodes, and gamescope (and through it XWayland's
 * GBM/EGL initialisation) stops without one. This adapter answers
 * gamescope's VK_EXT_physical_device_drm query with the real device numbers
 * of the session's Mali render node. It never opens the GPU itself.
 */
#define _GNU_SOURCE
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan_core.h>
#include <vulkan/vk_icd.h>
#include <dirent.h>
#include <dlfcn.h>
#include <limits.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>

#ifndef DD_MALI_NODE
#define DD_MALI_NODE "/dev/mali0"
#endif
#ifndef DD_BACKEND_NAME
#define DD_BACKEND_NAME "libpanvk_backend.so"
#endif
#define DD_EXPORT __attribute__((visibility("default")))
#define DD_MAX_EXTENSIONS 4096u

static pthread_once_t backend_once = PTHREAD_ONCE_INIT;
static void *backend_handle;
static PFN_vkGetInstanceProcAddr backend_gipa;
static PFN_GetPhysicalDeviceProcAddr backend_gpdpa;
static PFN_vkNegotiateLoaderICDInterfaceVersion backend_negotiate;
static _Atomic(PFN_vkGetPhysicalDeviceProperties) real_properties;
static _Atomic(PFN_vkGetPhysicalDeviceProperties2) real_properties2;
static _Atomic(PFN_vkEnumerateDeviceExtensionProperties) real_extensions;
static _Atomic(PFN_vkCreateDevice) real_create_device;

static void init_backend(void) {
    Dl_info info;
    char path[PATH_MAX];
    if (!dladdr((void *)&init_backend, &info) || !info.dli_fname) {
        fprintf(stderr, "DD_MALI_BACKEND_LOAD_FAILED: cannot locate adapter\n");
        return;
    }
    char *resolved = realpath(info.dli_fname, NULL);
    const char *own_path = resolved ? resolved : info.dli_fname;
    const char *slash = strrchr(own_path, '/');
    size_t prefix = slash ? (size_t)(slash - own_path + 1) : 0;
    if (prefix + sizeof(DD_BACKEND_NAME) > sizeof(path)) {
        fprintf(stderr, "DD_MALI_BACKEND_LOAD_FAILED: path too long\n");
        free(resolved);
        return;
    }
    memcpy(path, own_path, prefix);
    memcpy(path + prefix, DD_BACKEND_NAME, sizeof(DD_BACKEND_NAME));
    free(resolved);
    backend_handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!backend_handle) {
        fprintf(stderr, "DD_MALI_BACKEND_LOAD_FAILED: %s: %s\n", path, dlerror());
        return;
    }
    backend_gipa = (PFN_vkGetInstanceProcAddr)dlsym(backend_handle, "vk_icdGetInstanceProcAddr");
    backend_gpdpa = (PFN_GetPhysicalDeviceProcAddr)dlsym(backend_handle, "vk_icdGetPhysicalDeviceProcAddr");
    backend_negotiate = (PFN_vkNegotiateLoaderICDInterfaceVersion)dlsym(
        backend_handle, "vk_icdNegotiateLoaderICDInterfaceVersion");
    if (!backend_gipa || !backend_negotiate) {
        fprintf(stderr, "DD_MALI_BACKEND_LOAD_FAILED: backend lacks required ICD entry points\n");
        backend_gipa = NULL;
    }
}

static int ready(void) {
    pthread_once(&backend_once, init_backend);
    return backend_gipa != NULL;
}

static int node_rdev(const char *path, dev_t *rdev) {
    struct stat st;
    if (stat(path, &st) != 0 || !S_ISCHR(st.st_mode)) return 0;
    *rdev = st.st_rdev;
    return 1;
}

/* The session's Mali node: /dev/mali0 when it is exposed (the G615 layout),
 * otherwise the render node DroidDeck binds it as under /dev/dri. */
static int mali_node(dev_t *rdev) {
    if (node_rdev(DD_MALI_NODE, rdev)) return 1;
    DIR *d = opendir("/dev/dri");
    if (!d) return 0;
    int found = 0;
    struct dirent *e;
    while (!found && (e = readdir(d)) != NULL) {
        if (strncmp(e->d_name, "renderD", 7) != 0) continue;
        char path[PATH_MAX];
        int n = snprintf(path, sizeof(path), "/dev/dri/%s", e->d_name);
        if (n > 0 && (size_t)n < sizeof(path)) found = node_rdev(path, rdev);
    }
    closedir(d);
    return found;
}

static int target_properties(const VkPhysicalDeviceProperties *p) {
    return p->vendorID == 0x13b5u && strncmp(p->deviceName, "Mali-", 5) == 0;
}

static int target_device(VkPhysicalDevice device) {
    PFN_vkGetPhysicalDeviceProperties fn = atomic_load(&real_properties);
    if (!fn) return 0;
    VkPhysicalDeviceProperties p;
    memset(&p, 0, sizeof(p));
    fn(device, &p);
    dev_t dev;
    return target_properties(&p) && mali_node(&dev);
}

static VKAPI_ATTR void VKAPI_CALL wrapped_properties2(
    VkPhysicalDevice device, VkPhysicalDeviceProperties2 *properties) {
    PFN_vkGetPhysicalDeviceProperties2 fn = atomic_load(&real_properties2);
    if (!fn || !properties) return;
    fn(device, properties);
    dev_t rdev;
    if (!target_properties(&properties->properties) || !mali_node(&rdev)) return;
    /* Preserve every unrelated pNext structure and every ordinary property. */
    for (VkBaseOutStructure *p = (VkBaseOutStructure *)properties->pNext; p; p = p->pNext) {
        if (p->sType != VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRM_PROPERTIES_EXT) continue;
        VkPhysicalDeviceDrmPropertiesEXT *drm = (VkPhysicalDeviceDrmPropertiesEXT *)p;
        drm->hasPrimary = VK_TRUE;
        drm->hasRender = VK_TRUE;
        drm->primaryMajor = drm->renderMajor = (int64_t)major(rdev);
        drm->primaryMinor = drm->renderMinor = (int64_t)minor(rdev);
    }
}

static VKAPI_ATTR VkResult VKAPI_CALL wrapped_extensions(
    VkPhysicalDevice device, const char *layer, uint32_t *count, VkExtensionProperties *out) {
    PFN_vkEnumerateDeviceExtensionProperties fn = atomic_load(&real_extensions);
    if (!fn || !count) return VK_ERROR_INITIALIZATION_FAILED;
    if (layer || !target_device(device)) return fn(device, layer, count, out);
    uint32_t n = 0;
    VkResult r = fn(device, NULL, &n, NULL);
    if (r != VK_SUCCESS) return r;
    if (n > DD_MAX_EXTENSIONS) return VK_ERROR_OUT_OF_HOST_MEMORY;
    VkExtensionProperties *all = calloc((size_t)n + 1, sizeof(*all));
    if (!all) return VK_ERROR_OUT_OF_HOST_MEMORY;
    uint32_t actual = n;
    r = fn(device, NULL, &actual, all);
    if (r != VK_SUCCESS || actual > n) {
        free(all);
        return r == VK_SUCCESS ? VK_ERROR_INITIALIZATION_FAILED : r;
    }
    n = actual;
    int found = 0;
    for (uint32_t i = 0; i < n; ++i)
        if (!strcmp(all[i].extensionName, VK_EXT_PHYSICAL_DEVICE_DRM_EXTENSION_NAME)) found = 1;
    if (!found) {
        snprintf(all[n].extensionName, sizeof(all[n].extensionName), "%s",
                 VK_EXT_PHYSICAL_DEVICE_DRM_EXTENSION_NAME);
        all[n].specVersion = VK_EXT_PHYSICAL_DEVICE_DRM_SPEC_VERSION;
        ++n;
    }
    if (!out) {
        *count = n;
        free(all);
        return VK_SUCCESS;
    }
    uint32_t capacity = *count;
    uint32_t copied = capacity < n ? capacity : n;
    memcpy(out, all, (size_t)copied * sizeof(*all));
    *count = copied;
    free(all);
    return copied < n ? VK_INCOMPLETE : VK_SUCCESS;
}

static VKAPI_ATTR VkResult VKAPI_CALL wrapped_create_device(
    VkPhysicalDevice physical, const VkDeviceCreateInfo *info,
    const VkAllocationCallbacks *allocator, VkDevice *device) {
    PFN_vkCreateDevice fn = atomic_load(&real_create_device);
    if (!fn || !info) return VK_ERROR_INITIALIZATION_FAILED;
    if (!target_device(physical) || !info->enabledExtensionCount)
        return fn(physical, info, allocator, device);
    /* This adapter implements the metadata-only extension. It has no device
     * commands, and a backend which did not advertise it must not receive it. */
    uint32_t n = info->enabledExtensionCount;
    if (n > DD_MAX_EXTENSIONS) return VK_ERROR_OUT_OF_HOST_MEMORY;
    const char **names = calloc(n, sizeof(*names));
    if (!names) return VK_ERROR_OUT_OF_HOST_MEMORY;
    VkDeviceCreateInfo copy = *info;
    copy.enabledExtensionCount = 0;
    for (uint32_t i = 0; i < n; ++i) {
        const char *name = info->ppEnabledExtensionNames[i];
        if (strcmp(name, VK_EXT_PHYSICAL_DEVICE_DRM_EXTENSION_NAME))
            names[copy.enabledExtensionCount++] = name;
    }
    copy.ppEnabledExtensionNames = names;
    VkResult r = fn(physical, &copy, allocator, device);
    free(names);
    return r;
}

DD_EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_icdGetInstanceProcAddr(VkInstance, const char *);
DD_EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_icdGetPhysicalDeviceProcAddr(VkInstance, const char *);
DD_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vk_icdNegotiateLoaderICDInterfaceVersion(uint32_t *);
DD_EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance, const char *);

static PFN_vkVoidFunction map_proc(VkInstance instance, const char *name, PFN_vkVoidFunction proc) {
    if (!proc) return NULL;
    PFN_vkGetPhysicalDeviceProperties props = (PFN_vkGetPhysicalDeviceProperties)
        backend_gipa(instance, "vkGetPhysicalDeviceProperties");
    if (props) atomic_store(&real_properties, props);
    if (!strcmp(name, "vkGetPhysicalDeviceProperties2") ||
        !strcmp(name, "vkGetPhysicalDeviceProperties2KHR")) {
        atomic_store(&real_properties2, (PFN_vkGetPhysicalDeviceProperties2)proc);
        return (PFN_vkVoidFunction)wrapped_properties2;
    }
    if (!strcmp(name, "vkEnumerateDeviceExtensionProperties")) {
        atomic_store(&real_extensions, (PFN_vkEnumerateDeviceExtensionProperties)proc);
        return (PFN_vkVoidFunction)wrapped_extensions;
    }
    if (!strcmp(name, "vkCreateDevice")) {
        atomic_store(&real_create_device, (PFN_vkCreateDevice)proc);
        return (PFN_vkVoidFunction)wrapped_create_device;
    }
    return proc;
}

DD_EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_icdGetInstanceProcAddr(
    VkInstance instance, const char *name) {
    if (!name) return NULL;
    if (!strcmp(name, "vkGetInstanceProcAddr") || !strcmp(name, "vk_icdGetInstanceProcAddr"))
        return (PFN_vkVoidFunction)vk_icdGetInstanceProcAddr;
    if (!strcmp(name, "vk_icdGetPhysicalDeviceProcAddr"))
        return (PFN_vkVoidFunction)vk_icdGetPhysicalDeviceProcAddr;
    if (!strcmp(name, "vk_icdNegotiateLoaderICDInterfaceVersion"))
        return (PFN_vkVoidFunction)vk_icdNegotiateLoaderICDInterfaceVersion;
    if (!ready()) return NULL;
    PFN_vkVoidFunction proc = backend_gipa(instance, name);
    return map_proc(instance, name, proc);
}

DD_EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(
    VkInstance instance, const char *name) {
    return vk_icdGetInstanceProcAddr(instance, name);
}

DD_EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_icdGetPhysicalDeviceProcAddr(
    VkInstance instance, const char *name) {
    if (!name || !ready()) return NULL;
    PFN_vkVoidFunction proc = backend_gpdpa ? backend_gpdpa(instance, name) : backend_gipa(instance, name);
    return map_proc(instance, name, proc);
}

DD_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vk_icdNegotiateLoaderICDInterfaceVersion(uint32_t *version) {
    if (!version || *version < 1 || !ready()) return VK_ERROR_INCOMPATIBLE_DRIVER;
    /* Interface 7 is the newest protocol implemented by this adapter. */
    if (*version > 7) *version = 7;
    return backend_negotiate(version);
}
