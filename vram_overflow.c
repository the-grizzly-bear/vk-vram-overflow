#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <vulkan/vk_layer.h>
#include <stdio.h>
#include <string.h>

#ifndef VK_LAYER_EXPORT
#define VK_LAYER_EXPORT __attribute__((visibility("default")))
#endif

static PFN_vkGetInstanceProcAddr g_gipa = NULL;
static PFN_vkGetDeviceProcAddr   g_gdpa = NULL;
static PFN_vkAllocateMemory      g_realAllocateMemory = NULL;
static VkInstance                g_instance = VK_NULL_HANDLE;
static VkPhysicalDeviceMemoryProperties g_memProps;
static int g_haveMemProps = 0;

#define LOG(...) do { fprintf(stderr, "[vram_overflow] " __VA_ARGS__); fputc('\n', stderr); fflush(stderr); } while (0)

static int find_host_visible_type(void) {
    for (uint32_t i = 0; i < g_memProps.memoryTypeCount; i++) {
        VkMemoryPropertyFlags f = g_memProps.memoryTypes[i].propertyFlags;
        if ((f & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) &&
            !(f & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
            return (int)i;
    }
    return -1;
}

static VKAPI_ATTR VkResult VKAPI_CALL
layer_vkAllocateMemory(VkDevice device, const VkMemoryAllocateInfo *pInfo,
                       const VkAllocationCallbacks *pAlloc, VkDeviceMemory *pMem) {
    VkResult r = g_realAllocateMemory(device, pInfo, pAlloc, pMem);
    if (r != VK_ERROR_OUT_OF_DEVICE_MEMORY || !g_haveMemProps)
        return r;

    uint32_t idx = pInfo->memoryTypeIndex;
    VkMemoryPropertyFlags flags =
        (idx < g_memProps.memoryTypeCount) ? g_memProps.memoryTypes[idx].propertyFlags : 0;
    if (!(flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
        return r;

    int hv = find_host_visible_type();
    if (hv < 0)
        return r;

    VkMemoryAllocateInfo alt = *pInfo;
    alt.memoryTypeIndex = (uint32_t)hv;
    VkResult r2 = g_realAllocateMemory(device, &alt, pAlloc, pMem);
    if (r2 == VK_SUCCESS) {
        LOG("VRAM full: spilled %llu MiB to system RAM (memtype %u -> %d)",
            (unsigned long long)(pInfo->allocationSize >> 20), idx, hv);
        return VK_SUCCESS;
    }
    return r;
}

static VKAPI_ATTR VkResult VKAPI_CALL
layer_vkCreateDevice(VkPhysicalDevice phys, const VkDeviceCreateInfo *pCreateInfo,
                     const VkAllocationCallbacks *pAlloc, VkDevice *pDevice) {
    VkLayerDeviceCreateInfo *ci = (VkLayerDeviceCreateInfo *)pCreateInfo->pNext;
    while (ci && !(ci->sType == VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO &&
                   ci->function == VK_LAYER_LINK_INFO))
        ci = (VkLayerDeviceCreateInfo *)ci->pNext;
    if (!ci) return VK_ERROR_INITIALIZATION_FAILED;

    PFN_vkGetInstanceProcAddr gipa = ci->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    PFN_vkGetDeviceProcAddr   gdpa = ci->u.pLayerInfo->pfnNextGetDeviceProcAddr;
    ci->u.pLayerInfo = ci->u.pLayerInfo->pNext;

    PFN_vkCreateDevice createDevice = (PFN_vkCreateDevice)gipa(NULL, "vkCreateDevice");
    if (!createDevice) return VK_ERROR_INITIALIZATION_FAILED;

    VkResult r = createDevice(phys, pCreateInfo, pAlloc, pDevice);
    if (r != VK_SUCCESS) return r;

    g_gdpa = gdpa;
    g_realAllocateMemory = (PFN_vkAllocateMemory)gdpa(*pDevice, "vkAllocateMemory");

    PFN_vkGetPhysicalDeviceMemoryProperties gpdmp =
        (PFN_vkGetPhysicalDeviceMemoryProperties)g_gipa(g_instance, "vkGetPhysicalDeviceMemoryProperties");
    if (gpdmp) { gpdmp(phys, &g_memProps); g_haveMemProps = 1; }

    LOG("device ready; host-visible fallback type = %d", find_host_visible_type());
    return VK_SUCCESS;
}

static VKAPI_ATTR VkResult VKAPI_CALL
layer_vkCreateInstance(const VkInstanceCreateInfo *pCreateInfo,
                       const VkAllocationCallbacks *pAlloc, VkInstance *pInstance) {
    VkLayerInstanceCreateInfo *ci = (VkLayerInstanceCreateInfo *)pCreateInfo->pNext;
    while (ci && !(ci->sType == VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO &&
                   ci->function == VK_LAYER_LINK_INFO))
        ci = (VkLayerInstanceCreateInfo *)ci->pNext;
    if (!ci) return VK_ERROR_INITIALIZATION_FAILED;

    PFN_vkGetInstanceProcAddr gipa = ci->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    ci->u.pLayerInfo = ci->u.pLayerInfo->pNext;

    PFN_vkCreateInstance createInstance = (PFN_vkCreateInstance)gipa(NULL, "vkCreateInstance");
    if (!createInstance) return VK_ERROR_INITIALIZATION_FAILED;

    VkResult r = createInstance(pCreateInfo, pAlloc, pInstance);
    if (r != VK_SUCCESS) return r;

    g_gipa = gipa;
    g_instance = *pInstance;
    return VK_SUCCESS;
}

static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
layer_vkGetDeviceProcAddr(VkDevice device, const char *name) {
    if (!strcmp(name, "vkAllocateMemory")) return (PFN_vkVoidFunction)layer_vkAllocateMemory;
    if (!strcmp(name, "vkGetDeviceProcAddr")) return (PFN_vkVoidFunction)layer_vkGetDeviceProcAddr;
    return g_gdpa ? g_gdpa(device, name) : NULL;
}

static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
layer_vkGetInstanceProcAddr(VkInstance instance, const char *name) {
    if (!strcmp(name, "vkGetInstanceProcAddr")) return (PFN_vkVoidFunction)layer_vkGetInstanceProcAddr;
    if (!strcmp(name, "vkCreateInstance"))       return (PFN_vkVoidFunction)layer_vkCreateInstance;
    if (!strcmp(name, "vkCreateDevice"))         return (PFN_vkVoidFunction)layer_vkCreateDevice;
    if (!strcmp(name, "vkGetDeviceProcAddr"))    return (PFN_vkVoidFunction)layer_vkGetDeviceProcAddr;
    return g_gipa ? g_gipa(instance, name) : NULL;
}

VK_LAYER_EXPORT VKAPI_ATTR VkResult VKAPI_CALL
vkNegotiateLoaderLayerInterfaceVersion(VkNegotiateLayerInterface *pVersionStruct) {
    if (pVersionStruct->loaderLayerInterfaceVersion > 2)
        pVersionStruct->loaderLayerInterfaceVersion = 2;
    pVersionStruct->pfnGetInstanceProcAddr = layer_vkGetInstanceProcAddr;
    pVersionStruct->pfnGetDeviceProcAddr   = layer_vkGetDeviceProcAddr;
    pVersionStruct->pfnGetPhysicalDeviceProcAddr = NULL;
    return VK_SUCCESS;
}
