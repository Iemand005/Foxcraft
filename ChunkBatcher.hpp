#pragma once

#include <vector>
#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>

#include <glm/glm.hpp>
#include <vulkan/vulkan.h>

#include <Graphics/VulkanDevice.hpp>
#include <Graphics/VulkanGPUTexture.hpp>
#include <Vertex.hpp>

class ChunkBatcher {
public:
    ChunkBatcher(VulkanDevice* device,
                 const std::vector<std::string>& texturePaths,
                 VkDeviceSize maxVertexBytes = 128ull * 1024 * 1024,
                 VkDeviceSize maxIndexBytes = 64ull * 1024 * 1024,
                 uint32_t maxChunks = 10000)
        : device_(device)
        , maxVertexBytes_(maxVertexBytes)
        , maxIndexBytes_(maxIndexBytes)
        , maxChunks_(maxChunks)
    {
        CreateGiantBuffers();
        CreateIndirectBuffers();
        AllocateDescriptorSets();
        InitTexture(texturePaths);
        InitStaging();
    }

    ~ChunkBatcher() {
        VkDevice dev = device_->GetDevice();
        vkDeviceWaitIdle(dev);
        if (vertexBuffer_) {
            vkDestroyBuffer(dev, vertexBuffer_, nullptr);
            vkFreeMemory(dev, vertexMemory_, nullptr);
        }
        if (indexBuffer_) {
            vkDestroyBuffer(dev, indexBuffer_, nullptr);
            vkFreeMemory(dev, indexMemory_, nullptr);
        }
        for (int i = 0; i < kFrames; i++) {
            if (indirectBuffers_[i]) {
                vkDestroyBuffer(dev, indirectBuffers_[i], nullptr);
                vkFreeMemory(dev, indirectMemory_[i], nullptr);
            }
        }
        CleanupStaging();
    }

    struct SlotHandle {
        uint32_t index;
    };

    SlotHandle UploadChunk(const std::vector<fe::VertexArray>& vertices,
                           const std::vector<uint32_t>& indices,
                           const glm::vec3& worldOffset) {
        uint32_t vertexBytes = static_cast<uint32_t>(vertices.size() * sizeof(fe::VertexArray));
        uint32_t indexBytes = static_cast<uint32_t>(indices.size() * sizeof(uint32_t));

        std::vector<fe::VertexArray> worldVerts = vertices;
        for (auto& v : worldVerts) {
            v.position += worldOffset;
        }

        uint32_t vOff = nextVertexOffset_;
        uint32_t iOff = nextIndexOffset_;

        uint32_t frame = device_->GetCurrentFrame();
        uint32_t totalBytes = AlignUp(vertexBytes, 4) + AlignUp(indexBytes, 4);
        if (stagingCursor_[frame] + totalBytes > kStagingSize)
            throw std::runtime_error("ChunkBatcher staging buffer overflow");

        uint8_t* base = static_cast<uint8_t*>(stagingMapped_[frame]) + stagingCursor_[frame];
        std::memcpy(base, worldVerts.data(), vertexBytes);
        std::memcpy(base + AlignUp(vertexBytes, 4), indices.data(), indexBytes);

        pendingCopies_[frame].push_back({vertexBuffer_, vOff, stagingCursor_[frame], vertexBytes});
        pendingCopies_[frame].push_back({indexBuffer_, iOff, stagingCursor_[frame] + AlignUp(vertexBytes, 4), indexBytes});

        stagingCursor_[frame] += totalBytes;

        Slot slot;
        slot.vertexOffset = vOff;
        slot.vertexCount = static_cast<uint32_t>(vertices.size());
        slot.indexOffset = iOff;
        slot.indexCount = static_cast<uint32_t>(indices.size());
        slot.center = worldOffset + glm::vec3(16.0f, 64.0f, 16.0f);
        slot.used = true;

        nextVertexOffset_ += AlignUp(vertexBytes, 4);
        nextIndexOffset_ += AlignUp(indexBytes, 4);

        slots_.push_back(slot);
        return { static_cast<uint32_t>(slots_.size() - 1) };
    }

    void RemoveChunk(SlotHandle handle) {
        if (handle.index < slots_.size()) {
            slots_[handle.index].used = false;
        }
    }

    void SetFrustumCullingEnabled(bool enabled) { enableFrustumCulling_ = enabled; }

    void Update(const glm::vec3& cameraPos, const glm::vec3& cameraFront, float farPlane) {
        FlushUploads();

        cmds_.clear();

        glm::vec3 camDir = glm::normalize(cameraFront);

        for (auto& slot : slots_) {
            if (!slot.used) continue;

            if (enableFrustumCulling_) {
                glm::vec3 toCenter = slot.center - cameraPos;
                float dist = glm::length(toCenter);
                if (dist > farPlane) continue;
                if (glm::dot(glm::normalize(toCenter), camDir) < -0.2f) continue;
            }

            VkDrawIndexedIndirectCommand cmd{};
            cmd.indexCount = slot.indexCount;
            cmd.instanceCount = 1;
            cmd.firstIndex = slot.indexOffset / sizeof(uint32_t);
            cmd.vertexOffset = slot.vertexOffset / sizeof(fe::VertexArray);
            cmd.firstInstance = 0;
            cmds_.push_back(cmd);
        }

        uint32_t frame = device_->GetCurrentFrame();
        if (frame < kFrames && indirectMapped_[frame] && !cmds_.empty()) {
            memcpy(indirectMapped_[frame], cmds_.data(),
                   cmds_.size() * sizeof(VkDrawIndexedIndirectCommand));
        }
    }

    void Draw() {
        VkDevice dev = device_->GetDevice();
        VkCommandBuffer cmd = device_->GetCurrentCommandBuffer();
        if (!cmd || cmds_.empty()) return;

        uint32_t frame = device_->GetCurrentFrame();
        if (frame >= kFrames) return;

        device_->UpdateIndirectDescriptorSet(descriptorSets_[frame],
            chunkTexture_->imageView, chunkTexture_->sampler);

        device_->DrawIndirect(vertexBuffer_, indexBuffer_,
            indirectBuffers_[frame], 0,
            static_cast<uint32_t>(cmds_.size()),
            sizeof(VkDrawIndexedIndirectCommand),
            descriptorSets_[frame]);
    }

private:
    struct Slot {
        uint32_t vertexOffset;
        uint32_t vertexCount;
        uint32_t indexOffset;
        uint32_t indexCount;
        glm::vec3 center{};
        bool used = false;
    };

    static constexpr int kFrames = kMaxFramesInFlight;

    static uint32_t AlignUp(uint32_t val, uint32_t align) {
        return (val + align - 1) & ~(align - 1);
    }

    static uint32_t FindMemoryType(VkPhysicalDevice physDev, uint32_t typeFilter, VkMemoryPropertyFlags props) {
        VkPhysicalDeviceMemoryProperties memProps;
        vkGetPhysicalDeviceMemoryProperties(physDev, &memProps);
        for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
            if ((typeFilter & (1 << i)) && (memProps.memoryTypes[i].propertyFlags & props) == props)
                return i;
        }
        throw std::runtime_error("Failed to find suitable memory type");
    }

    static void CreateVkBuffer(VkDevice dev, VkPhysicalDevice physDev,
        VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags props,
        VkBuffer& buffer, VkDeviceMemory& memory) {
        VkBufferCreateInfo binfo{};
        binfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        binfo.size = size;
        binfo.usage = usage;
        binfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        vkCreateBuffer(dev, &binfo, nullptr, &buffer);

        VkMemoryRequirements memReq;
        vkGetBufferMemoryRequirements(dev, buffer, &memReq);
        VkMemoryAllocateInfo ainfo{};
        ainfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ainfo.allocationSize = memReq.size;
        ainfo.memoryTypeIndex = FindMemoryType(physDev, memReq.memoryTypeBits, props);
        vkAllocateMemory(dev, &ainfo, nullptr, &memory);
        vkBindBufferMemory(dev, buffer, memory, 0);
    }

    void CreateGiantBuffers() {
        VkDevice dev = device_->GetDevice();
        VkPhysicalDevice physDev = device_->GetPhysicalDevice();
        CreateVkBuffer(dev, physDev, maxVertexBytes_,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            vertexBuffer_, vertexMemory_);
        CreateVkBuffer(dev, physDev, maxIndexBytes_,
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            indexBuffer_, indexMemory_);
    }

    void CreateIndirectBuffers() {
        VkDevice dev = device_->GetDevice();
        VkPhysicalDevice physDev = device_->GetPhysicalDevice();
        VkDeviceSize bufSize = maxChunks_ * sizeof(VkDrawIndexedIndirectCommand);
        for (int i = 0; i < kFrames; i++) {
            CreateVkBuffer(dev, physDev, bufSize,
                VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                indirectBuffers_[i], indirectMemory_[i]);
            vkMapMemory(dev, indirectMemory_[i], 0, bufSize, 0, &indirectMapped_[i]);
        }
    }

    void AllocateDescriptorSets() {
        VkDevice dev = device_->GetDevice();
        VkDescriptorSetLayout layout = device_->GetDescriptorSetLayout();
        std::vector<VkDescriptorSetLayout> layouts(kFrames, layout);
        VkDescriptorSetAllocateInfo ainfo{};
        ainfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        ainfo.descriptorPool = device_->GetDescriptorPool();
        ainfo.descriptorSetCount = kFrames;
        ainfo.pSetLayouts = layouts.data();
        if (vkAllocateDescriptorSets(dev, &ainfo, descriptorSets_) != VK_SUCCESS) {
            throw std::runtime_error("Failed to allocate batcher descriptor sets");
        }
    }

    void InitTexture(const std::vector<std::string>& texturePaths) {
        chunkTexture_ = std::make_unique<fe::VulkanGPUTexture>();
        chunkTexture_->uploadTextureArray(
            device_->GetDevice(),
            device_->GetPhysicalDevice(),
            device_->GetCommandPool(),
            device_->GetGraphicsQueue(),
            texturePaths,
            fe::TextureScaling::Nearest);
    }

    struct PendingCopy {
        VkBuffer dst;
        VkDeviceSize dstOffset;
        VkDeviceSize srcOffset;
        VkDeviceSize size;
    };

    void InitStaging() {
        VkDevice dev = device_->GetDevice();
        VkPhysicalDevice physDev = device_->GetPhysicalDevice();
        VkCommandPool cmdPool = device_->GetCommandPool();
        VkDeviceSize bufSize = kStagingSize;

        for (int i = 0; i < kFrames; i++) {
            CreateVkBuffer(dev, physDev, bufSize,
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                stagingBuffers_[i], stagingMemory_[i]);
            vkMapMemory(dev, stagingMemory_[i], 0, bufSize, 0, &stagingMapped_[i]);

            VkCommandBufferAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            allocInfo.commandPool = cmdPool;
            allocInfo.commandBufferCount = 1;
            vkAllocateCommandBuffers(dev, &allocInfo, &stagingCmdBufs_[i]);

            VkFenceCreateInfo finfo{};
            finfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            finfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
            vkCreateFence(dev, &finfo, nullptr, &stagingFences_[i]);
        }
    }

    void CleanupStaging() {
        VkDevice dev = device_->GetDevice();
        for (int i = 0; i < kFrames; i++) {
            if (stagingBuffers_[i]) {
                vkDestroyBuffer(dev, stagingBuffers_[i], nullptr);
                vkFreeMemory(dev, stagingMemory_[i], nullptr);
                stagingBuffers_[i] = VK_NULL_HANDLE;
            }
            if (stagingCmdBufs_[i]) {
                vkFreeCommandBuffers(dev, device_->GetCommandPool(), 1, &stagingCmdBufs_[i]);
                stagingCmdBufs_[i] = VK_NULL_HANDLE;
            }
            if (stagingFences_[i]) {
                vkDestroyFence(dev, stagingFences_[i], nullptr);
                stagingFences_[i] = VK_NULL_HANDLE;
            }
        }
    }

    void FlushUploads() {
        VkDevice dev = device_->GetDevice();
        VkQueue gfxQueue = device_->GetGraphicsQueue();
        uint32_t frame = device_->GetCurrentFrame();

        vkWaitForFences(dev, 1, &stagingFences_[frame], VK_TRUE, UINT64_MAX);

        if (pendingCopies_[frame].empty()) {
            stagingCursor_[frame] = 0;
            return;
        }

        vkResetFences(dev, 1, &stagingFences_[frame]);

        vkResetCommandBuffer(stagingCmdBufs_[frame], 0);
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(stagingCmdBufs_[frame], &beginInfo);

        for (auto& copy : pendingCopies_[frame]) {
            VkBufferCopy bufCopy{};
            bufCopy.srcOffset = copy.srcOffset;
            bufCopy.dstOffset = copy.dstOffset;
            bufCopy.size = copy.size;
            vkCmdCopyBuffer(stagingCmdBufs_[frame], stagingBuffers_[frame], copy.dst, 1, &bufCopy);
        }

        vkEndCommandBuffer(stagingCmdBufs_[frame]);

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &stagingCmdBufs_[frame];
        vkQueueSubmit(gfxQueue, 1, &submitInfo, stagingFences_[frame]);

        pendingCopies_[frame].clear();
        stagingCursor_[frame] = 0;
    }

    VulkanDevice* device_;
    VkDeviceSize maxVertexBytes_;
    VkDeviceSize maxIndexBytes_;
    uint32_t maxChunks_;
    bool enableFrustumCulling_ = true;

    VkBuffer vertexBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory vertexMemory_ = VK_NULL_HANDLE;
    VkBuffer indexBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory indexMemory_ = VK_NULL_HANDLE;

    VkBuffer indirectBuffers_[kFrames] = {};
    VkDeviceMemory indirectMemory_[kFrames] = {};
    void* indirectMapped_[kFrames] = {};

    VkDescriptorSet descriptorSets_[kFrames] = {};

    std::unique_ptr<fe::VulkanGPUTexture> chunkTexture_;

    uint32_t nextVertexOffset_ = 0;
    uint32_t nextIndexOffset_ = 0;

    std::vector<Slot> slots_;
    std::vector<VkDrawIndexedIndirectCommand> cmds_;

    static constexpr VkDeviceSize kStagingSize = 8ull * 1024 * 1024;

    VkBuffer stagingBuffers_[kFrames] = {};
    VkDeviceMemory stagingMemory_[kFrames] = {};
    void* stagingMapped_[kFrames] = {};
    uint32_t stagingCursor_[kFrames] = {};
    VkFence stagingFences_[kFrames] = {};
    VkCommandBuffer stagingCmdBufs_[kFrames] = {};
    std::vector<PendingCopy> pendingCopies_[kFrames];
};
