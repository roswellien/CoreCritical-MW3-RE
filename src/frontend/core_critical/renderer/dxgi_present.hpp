#pragma once
#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <d3d11_1.h>
#include <dxgi1_2.h>
#include <stdexcept>
#include <string>
#include <vulkan/vulkan_core.h>
#include <vulkan/vulkan_win32.h>
#include <windows.h>
#include <wrl/client.h>

namespace mw34::vkscene {
// Optional presentation transport. Vulkan and D3D11 must identify the same
// physical adapter. A shared texture and keyed mutex keep frame pixels on GPU.
// The renderer owns this object and must detach it before destroying VkDevice.
class DxgiPresent {
    template <class T> using Com = Microsoft::WRL::ComPtr<T>;
    Com<IDXGIFactory2> factory_;
    Com<ID3D11Device> d3d_;
    Com<ID3D11DeviceContext> context_;
    Com<IDXGISwapChain1> swap_;
    Com<ID3D11Texture2D> texture_;
    Com<IDXGIKeyedMutex> mutex_;
    VkDevice device_{};
    VkImage image_{};
    VkDeviceMemory memory_{};
    HWND window_{};
    unsigned width_{}, height_{}, family_{};
    VkExtent3D maximum_extent_{};
    bool initialized_{}, used_{};
    bool capture_requested_{}, capture_ready_{};
    unsigned capture_x_{}, capture_y_{};
    std::array<unsigned char, 4> captured_pixel_{};
    std::uint64_t acquire_key_{0}, release_key_{1};
    std::uint32_t timeout_{5000};
    PFN_vkCreateImage create_image_{};
    PFN_vkGetImageMemoryRequirements image_requirements_{};
    PFN_vkGetMemoryWin32HandlePropertiesKHR handle_properties_{};
    PFN_vkAllocateMemory allocate_{};
    PFN_vkBindImageMemory bind_{};
    PFN_vkDestroyImage destroy_image_{};
    PFN_vkFreeMemory free_{};
    PFN_vkDeviceWaitIdle idle_{};

    static void check_hr(HRESULT result, const char *operation) {
        if (FAILED(result))
            throw std::runtime_error(std::string("DXGI presentation: ") + operation + " failed (" +
                                     std::to_string(result) + ")");
    }
    static void check_vk(VkResult result, const char *operation) {
        if (result != VK_SUCCESS)
            throw std::runtime_error(std::string("DXGI Vulkan sharing: ") + operation +
                                     " failed (" + std::to_string(int(result)) + ")");
    }
    // WAIT_TIMEOUT and WAIT_ABANDONED are positive HRESULT values. Neither is a
    // successful mutex acquisition; SUCCEEDED alone would fabricate ownership.
    void acquire_mutex(std::uint64_t key) {
        const auto result = mutex_->AcquireSync(key, timeout_);
        if (result != S_OK)
            throw std::runtime_error("DXGI shared texture acquisition failed (" +
                                     std::to_string(result) + ")");
    }
    struct MutexLease {
        DxgiPresent &owner;
        bool released{};
        explicit MutexLease(DxgiPresent &value, std::uint64_t key) : owner(value) {
            owner.acquire_mutex(key);
        }
        void release() {
            check_hr(owner.mutex_->ReleaseSync(0), "release shared texture");
            released = true;
        }
        ~MutexLease() {
            if (!released)
                owner.mutex_->ReleaseSync(0);
        }
    };
    void release_texture() {
        if (used_) {
            check_vk(idle_(device_), "drain shared Vulkan image");
            MutexLease completed(*this, 0);
            completed.release();
        }
        if (image_)
            destroy_image_(device_, image_, nullptr);
        if (memory_)
            free_(device_, memory_, nullptr);
        image_ = {};
        memory_ = {};
        mutex_.Reset();
        texture_.Reset();
        initialized_ = used_ = false;
        width_ = height_ = 0;
    }
    template <class T> T load(PFN_vkGetDeviceProcAddr get, const char *name) {
        const auto function = reinterpret_cast<T>(get(device_, name));
        if (!function)
            throw std::runtime_error(std::string("DXGI sharing entry point missing: ") + name);
        return function;
    }

  public:
    DxgiPresent() = default;
    DxgiPresent(const DxgiPresent &) = delete;
    DxgiPresent &operator=(const DxgiPresent &) = delete;
    ~DxgiPresent() { detach(); }
    static bool requested() {
        const char *value = std::getenv("MW34_DXGI_PRESENT");
        if (!value || !std::strcmp(value, "0"))
            return false;
        if (!std::strcmp(value, "1"))
            return true;
        throw std::runtime_error("MW34_DXGI_PRESENT requires 0 or 1");
    }
    bool active() const { return device_ != VK_NULL_HANDLE; }
    VkImage image() const { return image_; }
    void attach(VkDevice device, const VkPhysicalDeviceIDProperties &identity, unsigned family,
                PFN_vkGetDeviceProcAddr get, VkExtent3D maximum_extent) {
        if (!identity.deviceLUIDValid)
            throw std::runtime_error("DXGI sharing requires a valid Vulkan adapter LUID");
        Com<IDXGIFactory1> factory;
        check_hr(::CreateDXGIFactory1(IID_PPV_ARGS(&factory)), "create adapter factory");
        check_hr(factory.As(&factory_), "query presentation factory");
        Com<IDXGIAdapter1> selected;
        for (unsigned index = 0;; ++index) {
            Com<IDXGIAdapter1> candidate;
            const auto result = factory->EnumAdapters1(index, &candidate);
            if (result == DXGI_ERROR_NOT_FOUND)
                break;
            check_hr(result, "enumerate adapter");
            DXGI_ADAPTER_DESC1 properties{};
            check_hr(candidate->GetDesc1(&properties), "read adapter identity");
            if (!std::memcmp(identity.deviceLUID, &properties.AdapterLuid, VK_LUID_SIZE)) {
                selected = candidate;
                break;
            }
        }
        if (!selected)
            throw std::runtime_error("No DXGI adapter matches the Vulkan device LUID");
        check_hr(::D3D11CreateDevice(selected.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr,
                                     D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                                     D3D11_SDK_VERSION, &d3d_, nullptr, &context_),
                 "create matching device");
        device_ = device;
        family_ = family;
        maximum_extent_ = maximum_extent;
#define MW34_DXGI_LOAD(member, name) member = load<PFN_##name>(get, #name)
        MW34_DXGI_LOAD(create_image_, vkCreateImage);
        MW34_DXGI_LOAD(image_requirements_, vkGetImageMemoryRequirements);
        MW34_DXGI_LOAD(handle_properties_, vkGetMemoryWin32HandlePropertiesKHR);
        MW34_DXGI_LOAD(allocate_, vkAllocateMemory);
        MW34_DXGI_LOAD(bind_, vkBindImageMemory);
        MW34_DXGI_LOAD(destroy_image_, vkDestroyImage);
        MW34_DXGI_LOAD(free_, vkFreeMemory);
        MW34_DXGI_LOAD(idle_, vkDeviceWaitIdle);
#undef MW34_DXGI_LOAD
    }
    void reset_window() {
        release_texture();
        swap_.Reset();
        if (context_) {
            context_->ClearState();
            context_->Flush();
        }
        window_ = {};
    }
    void detach() noexcept {
        if (device_) {
            try {
                reset_window();
            } catch (...) {
                // Errors on the live path are thrown before shutdown. Device
                // loss must not throw a second exception from the destructor.
                if (image_ && destroy_image_)
                    destroy_image_(device_, image_, nullptr);
                if (memory_ && free_)
                    free_(device_, memory_, nullptr);
                image_ = {};
                memory_ = {};
            }
        }
        swap_.Reset();
        mutex_.Reset();
        texture_.Reset();
        context_.Reset();
        d3d_.Reset();
        factory_.Reset();
        device_ = {};
        image_ = {};
        memory_ = {};
        window_ = {};
        width_ = height_ = family_ = 0;
        initialized_ = used_ = capture_requested_ = capture_ready_ = false;
    }
    void prepare(HWND window, unsigned width, unsigned height) {
        if (!width || !height || width > maximum_extent_.width || height > maximum_extent_.height)
            throw std::runtime_error("DXGI shared-image size exceeds the Vulkan format limits");
        if (window_ != window)
            reset_window();
        if (image_ && width == width_ && height == height_)
            return;
        release_texture();
        if (swap_) {
            context_->ClearState();
            context_->Flush();
            check_hr(swap_->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0),
                     "resize swapchain");
        } else {
            DXGI_SWAP_CHAIN_DESC1 description{};
            description.Width = width;
            description.Height = height;
            description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            description.SampleDesc.Count = 1;
            description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            description.BufferCount = 2;
            description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
            description.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
            check_hr(factory_->CreateSwapChainForHwnd(d3d_.Get(), window, &description, nullptr,
                                                      nullptr, &swap_),
                     "create swapchain");
            check_hr(factory_->MakeWindowAssociation(window, DXGI_MWA_NO_ALT_ENTER |
                                                                 DXGI_MWA_NO_WINDOW_CHANGES),
                     "own window mode");
        }
        window_ = window;
        D3D11_TEXTURE2D_DESC description{};
        description.Width = width;
        description.Height = height;
        description.MipLevels = description.ArraySize = 1;
        description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        description.SampleDesc.Count = 1;
        description.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
        description.MiscFlags =
            D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
        check_hr(d3d_->CreateTexture2D(&description, nullptr, &texture_), "create shared texture");
        check_hr(texture_.As(&mutex_), "query shared mutex");
        Com<IDXGIResource1> resource;
        check_hr(texture_.As(&resource), "query shared resource");
        struct Handle {
            HANDLE value{};
            ~Handle() {
                if (value)
                    ::CloseHandle(value);
            }
        } handle;
        check_hr(resource->CreateSharedHandle(
                     nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, nullptr,
                     &handle.value),
                 "export shared texture");
        VkExternalMemoryImageCreateInfo external{};
        external.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
        external.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT;
        VkImageCreateInfo image_info{};
        image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        image_info.pNext = &external;
        image_info.imageType = VK_IMAGE_TYPE_2D;
        image_info.format = VK_FORMAT_B8G8R8A8_UNORM;
        image_info.extent = {width, height, 1};
        image_info.mipLevels = image_info.arrayLayers = 1;
        image_info.samples = VK_SAMPLE_COUNT_1_BIT;
        image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
        image_info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                           VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        check_vk(create_image_(device_, &image_info, nullptr, &image_), "create imported image");
        VkMemoryRequirements requirements{};
        image_requirements_(device_, image_, &requirements);
        VkMemoryWin32HandlePropertiesKHR properties{};
        properties.sType = VK_STRUCTURE_TYPE_MEMORY_WIN32_HANDLE_PROPERTIES_KHR;
        check_vk(handle_properties_(device_, VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT,
                                    handle.value, &properties),
                 "query imported memory");
        const auto bits = requirements.memoryTypeBits & properties.memoryTypeBits;
        unsigned memory_type = 0;
        while (memory_type < 32 && !(bits & (1u << memory_type)))
            ++memory_type;
        if (memory_type == 32)
            throw std::runtime_error("DXGI texture has no compatible Vulkan memory type");
        VkMemoryDedicatedAllocateInfo dedicated{};
        dedicated.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO;
        dedicated.image = image_;
        VkImportMemoryWin32HandleInfoKHR import{};
        import.sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_WIN32_HANDLE_INFO_KHR;
        import.pNext = &dedicated;
        import.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT;
        import.handle = handle.value;
        VkMemoryAllocateInfo allocation{};
        allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocation.pNext = &import;
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex = memory_type;
        check_vk(allocate_(device_, &allocation, nullptr, &memory_), "import shared memory");
        check_vk(bind_(device_, image_, memory_, 0), "bind shared memory");
        width_ = width;
        height_ = height;
    }
    void acquire_image(VkCommandBuffer command, PFN_vkCmdPipelineBarrier barrier_command,
                       PFN_vkCmdClearColorImage clear_command) {
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = initialized_ ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_EXTERNAL;
        barrier.dstQueueFamilyIndex = family_;
        barrier.image = image_;
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier_command(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                        0, 0, nullptr, 0, nullptr, 1, &barrier);
        const VkClearColorValue black{};
        clear_command(command, image_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &black, 1,
                      &barrier.subresourceRange);
        barrier.oldLayout = barrier.newLayout;
        barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier_command(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                        0, nullptr, 0, nullptr, 1, &barrier);
    }
    void release_image(VkCommandBuffer command, PFN_vkCmdPipelineBarrier barrier_command) {
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.srcQueueFamilyIndex = family_;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_EXTERNAL;
        barrier.image = image_;
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier_command(command, VK_PIPELINE_STAGE_TRANSFER_BIT,
                        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1,
                        &barrier);
        initialized_ = used_ = true;
    }
    VkWin32KeyedMutexAcquireReleaseInfoKHR synchronization() const {
        VkWin32KeyedMutexAcquireReleaseInfoKHR info{};
        info.sType = VK_STRUCTURE_TYPE_WIN32_KEYED_MUTEX_ACQUIRE_RELEASE_INFO_KHR;
        info.acquireCount = info.releaseCount = 1;
        info.pAcquireSyncs = info.pReleaseSyncs = &memory_;
        info.pAcquireKeys = &acquire_key_;
        info.pReleaseKeys = &release_key_;
        info.pAcquireTimeouts = &timeout_;
        return info;
    }
    bool present(bool vsync) {
        bool captured = false;
        capture_ready_ = false;
        {
            MutexLease ready(*this, 1);
            Com<ID3D11Texture2D> back;
            check_hr(swap_->GetBuffer(0, IID_PPV_ARGS(&back)), "get presentation image");
            context_->CopyResource(back.Get(), texture_.Get());
            if (capture_requested_) {
                captured_pixel_ = copy_pixel(back.Get(), capture_x_, capture_y_);
                capture_requested_ = false;
                captured = true;
            }
            context_->Flush();
            ready.release();
        }
        const auto result = swap_->Present(vsync ? 1 : 0, 0);
        check_hr(result, "present");
        capture_ready_ = captured && result == S_OK;
        return result == S_OK;
    }
    // Explicit diagnostic readback, never part of normal presentation. It
    // verifies shared-image channel order and destination placement in tests.
    void request_pixel(unsigned x, unsigned y) {
        capture_x_ = x;
        capture_y_ = y;
        capture_requested_ = true;
        capture_ready_ = false;
    }
    std::array<unsigned char, 4> read_pixel() {
        if (!capture_ready_)
            throw std::runtime_error("No completed DXGI presentation pixel capture");
        capture_ready_ = false;
        return captured_pixel_;
    }

  private:
    std::array<unsigned char, 4> copy_pixel(ID3D11Texture2D *source, unsigned x, unsigned y) {
        if (!image_ || x >= width_ || y >= height_)
            throw std::runtime_error("DXGI diagnostic pixel outside the shared image");
        D3D11_TEXTURE2D_DESC description{};
        source->GetDesc(&description);
        description.Width = description.Height = 1;
        description.Usage = D3D11_USAGE_STAGING;
        description.BindFlags = description.MiscFlags = 0;
        description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        Com<ID3D11Texture2D> staging;
        check_hr(d3d_->CreateTexture2D(&description, nullptr, &staging), "create diagnostic pixel");
        const D3D11_BOX box{x, y, 0, x + 1, y + 1, 1};
        context_->CopySubresourceRegion(staging.Get(), 0, 0, 0, 0, source, 0, &box);
        D3D11_MAPPED_SUBRESOURCE mapped{};
        check_hr(context_->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped),
                 "read diagnostic pixel");
        std::array<unsigned char, 4> result{};
        std::memcpy(result.data(), mapped.pData, result.size());
        context_->Unmap(staging.Get(), 0);
        return result;
    }
};
} // namespace mw34::vkscene
