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

        UploadBufferData(vertexBuffer_, vertexMemory_, vOff,
            worldVerts.data(), vertexBytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        UploadBufferData(indexBuffer_, indexMemory_, iOff,
            indices.data(), indexBytes, VK_BUFFER_USAGE_INDEX_BUFFER_BIT);

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

    void Update(const glm::vec3& cameraPos, const glm::vec3& cameraFront, float farPlane) {
        cmds_.clear();

        glm::vec3 camDir = glm::normalize(cameraFront);

        for (auto& slot : slots_) {
            if (!slot.used) continue;

            glm::vec3 toCenter = slot.center - cameraPos;
            float dist = glm::length(toCenter);
            if (dist > farPlane) continue;
            if (glm::dot(glm::normalize(toCenter), camDir) < -0.2f) continue;

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

    void UploadBufferData(VkBuffer dstBuffer, VkDeviceMemory dstMemory,
        VkDeviceSize dstOffset, const void* data, VkDeviceSize dataSize,
        VkBufferUsageFlags usage) {
        if (dataSize == 0) return;
        VkDevice dev = device_->GetDevice();
        VkPhysicalDevice physDev = device_->GetPhysicalDevice();
        VkCommandPool cmdPool = device_->GetCommandPool();
        VkQueue gfxQueue = device_->GetGraphicsQueue();

        VkBuffer staging;
        VkDeviceMemory stagingMemory;
        CreateVkBuffer(dev, physDev, dataSize,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            staging, stagingMemory);

        void* mapped;
        vkMapMemory(dev, stagingMemory, 0, dataSize, 0, &mapped);
        memcpy(mapped, data, static_cast<size_t>(dataSize));
        vkUnmapMemory(dev, stagingMemory);

        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandPool = cmdPool;
        allocInfo.commandBufferCount = 1;

        VkCommandBuffer cmdBuf;
        vkAllocateCommandBuffers(dev, &allocInfo, &cmdBuf);

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmdBuf, &beginInfo);

        VkBufferCopy copy{};
        copy.srcOffset = 0;
        copy.dstOffset = dstOffset;
        copy.size = dataSize;
        vkCmdCopyBuffer(cmdBuf, staging, dstBuffer, 1, &copy);

        vkEndCommandBuffer(cmdBuf);

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &cmdBuf;
        vkQueueSubmit(gfxQueue, 1, &submitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(gfxQueue);

        vkFreeCommandBuffers(dev, cmdPool, 1, &cmdBuf);
        vkDestroyBuffer(dev, staging, nullptr);
        vkFreeMemory(dev, stagingMemory, nullptr);
    }

    VulkanDevice* device_;
    VkDeviceSize maxVertexBytes_;
    VkDeviceSize maxIndexBytes_;
    uint32_t maxChunks_;

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
};
