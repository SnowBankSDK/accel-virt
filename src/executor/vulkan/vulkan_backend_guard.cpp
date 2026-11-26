#include "executor/vulkan/vulkan_backend.hpp"
#include "core/log/driver_log.hpp"
#include "core/log/log_line.hpp"
#include "core/trace/function_scope.hpp"

namespace av {

namespace {

uint64_t number(const void *handle)
{
    AV_SCOPE("number");
    return reinterpret_cast<uint64_t>(handle);
}

}

void VulkanBackend::destroy(VulkanDestroyKind kind, uint64_t device, uint64_t parent, uint64_t handle)
{
    AV_SCOPE("VulkanBackend::destroy");
    const VkDevice made_by = reinterpret_cast<VkDevice>(device);
    switch (kind) {
    case VulkanDestroyKind::none: return;
    case VulkanDestroyKind::buffer:
        RealVulkanDispatch::on_vkDestroyBuffer(made_by, reinterpret_cast<VkBuffer>(handle), nullptr);
        return;
    case VulkanDestroyKind::image:
        RealVulkanDispatch::on_vkDestroyImage(made_by, reinterpret_cast<VkImage>(handle), nullptr);
        return;
    case VulkanDestroyKind::image_view:
        RealVulkanDispatch::on_vkDestroyImageView(made_by, reinterpret_cast<VkImageView>(handle), nullptr);
        return;
    case VulkanDestroyKind::buffer_view:
        RealVulkanDispatch::on_vkDestroyBufferView(made_by, reinterpret_cast<VkBufferView>(handle), nullptr);
        return;
    case VulkanDestroyKind::sampler:
        RealVulkanDispatch::on_vkDestroySampler(made_by, reinterpret_cast<VkSampler>(handle), nullptr);
        return;
    case VulkanDestroyKind::pipeline:
        RealVulkanDispatch::on_vkDestroyPipeline(made_by, reinterpret_cast<VkPipeline>(handle), nullptr);
        return;
    case VulkanDestroyKind::pipeline_layout:
        RealVulkanDispatch::on_vkDestroyPipelineLayout(made_by, reinterpret_cast<VkPipelineLayout>(handle), nullptr);
        return;
    case VulkanDestroyKind::render_pass:
        RealVulkanDispatch::on_vkDestroyRenderPass(made_by, reinterpret_cast<VkRenderPass>(handle), nullptr);
        return;
    case VulkanDestroyKind::framebuffer:
        RealVulkanDispatch::on_vkDestroyFramebuffer(made_by, reinterpret_cast<VkFramebuffer>(handle), nullptr);
        return;
    case VulkanDestroyKind::query_pool:
        RealVulkanDispatch::on_vkDestroyQueryPool(made_by, reinterpret_cast<VkQueryPool>(handle), nullptr);
        return;
    case VulkanDestroyKind::event:
        RealVulkanDispatch::on_vkDestroyEvent(made_by, reinterpret_cast<VkEvent>(handle), nullptr);
        return;
    case VulkanDestroyKind::descriptor_set_layout:
        RealVulkanDispatch::on_vkDestroyDescriptorSetLayout(made_by, reinterpret_cast<VkDescriptorSetLayout>(handle),
                                                            nullptr);
        return;
    case VulkanDestroyKind::descriptor_pool:
        RealVulkanDispatch::on_vkDestroyDescriptorPool(made_by, reinterpret_cast<VkDescriptorPool>(handle), nullptr);
        return;
    case VulkanDestroyKind::descriptor_pool_reset:
        static_cast<void>(RealVulkanDispatch::on_vkResetDescriptorPool(made_by, reinterpret_cast<VkDescriptorPool>(handle), 0u));
        return;
    case VulkanDestroyKind::command_pool:
        RealVulkanDispatch::on_vkDestroyCommandPool(made_by, reinterpret_cast<VkCommandPool>(handle), nullptr);
        return;
    case VulkanDestroyKind::memory:
        RealVulkanDispatch::on_vkFreeMemory(made_by, reinterpret_cast<VkDeviceMemory>(handle), nullptr);
        vram_.release(handle);
        return;
    case VulkanDestroyKind::acceleration_structure:
        RealVulkanDispatch::on_vkDestroyAccelerationStructureKHR(made_by, reinterpret_cast<VkAccelerationStructureKHR>(handle),
                                                                nullptr);
        return;
    case VulkanDestroyKind::shader:
        RealVulkanDispatch::on_vkDestroyShaderEXT(made_by, reinterpret_cast<VkShaderEXT>(handle), nullptr);
        return;
    case VulkanDestroyKind::indirect_commands_layout:
        RealVulkanDispatch::on_vkDestroyIndirectCommandsLayoutEXT(
            made_by, reinterpret_cast<VkIndirectCommandsLayoutEXT>(handle), nullptr);
        return;
    case VulkanDestroyKind::indirect_execution_set:
        RealVulkanDispatch::on_vkDestroyIndirectExecutionSetEXT(
            made_by, reinterpret_cast<VkIndirectExecutionSetEXT>(handle), nullptr);
        return;
    case VulkanDestroyKind::imported_memory: {
        GuestDevice *const owner = device_of(device);
        if (owner != nullptr) owner->memory().release(handle);
        return;
    }
    case VulkanDestroyKind::imported_semaphore: {
        GuestDevice *const owner = device_of(device);
        if (owner != nullptr) owner->memory().destroy_semaphore(handle);
        return;
    }
    case VulkanDestroyKind::descriptor_set: {
        const VkDescriptorSet set = reinterpret_cast<VkDescriptorSet>(handle);
        static_cast<void>(RealVulkanDispatch::on_vkFreeDescriptorSets(
            made_by, reinterpret_cast<VkDescriptorPool>(parent), 1u, &set));
        return;
    }
    case VulkanDestroyKind::command_buffer: {
        const VkCommandBuffer buffer = reinterpret_cast<VkCommandBuffer>(handle);
        RealVulkanDispatch::on_vkFreeCommandBuffers(made_by, reinterpret_cast<VkCommandPool>(parent), 1u, &buffer);
        return;
    }
    }
}

void VulkanBackend::release_children(uint64_t parent, uint64_t device)
{
    AV_SCOPE("VulkanBackend::release_children");
    handles_.for_each_child(parent, [this, device](uint64_t child) { static_cast<void>(guard_.release(child, device)); });
}

bool VulkanBackend::children_marks(uint64_t parent, uint64_t device, uint64_t *marks, uint32_t &count)
{
    AV_SCOPE("VulkanBackend::children_marks");
    count = 0u;
    bool   fits = true;
    const uint64_t reached = guard_.reached(device);
    handles_.for_each_child(parent, [&](uint64_t child) {
        const uint64_t mark = guard_.mark_of(child, device);
        if (mark <= reached) return;
        for (uint32_t index = 0u; index < count; index++)
            if (marks[index] == mark) return;
        if (count == max_child_marks) fits = false;
        else marks[count++] = mark;
    });
    return fits;
}

uint64_t VulkanBackend::device_of_buffer(uint64_t handle) const
{
    AV_SCOPE("VulkanBackend::device_of_buffer");
    if (buffer_device_.buffer_ == handle) return buffer_device_.device_;
    const uint64_t device = owning_device(handle);
    buffer_device_ = BufferDevice{handle, device};
    return device;
}

uint64_t VulkanBackend::owning_device(uint64_t handle) const
{
    AV_SCOPE("VulkanBackend::owning_device");
    for (uint64_t at = handle; at != 0u; at = handles_.parent_of(at))
        if (handles_.find(at, VulkanObjectKind::device) != nullptr) return at;
    return 0u;
}

void VulkanBackend::on_vkDestroyBuffer(VkDevice device, VkBuffer buffer, const VkAllocationCallbacks *)
{
    AV_SCOPE("VulkanBackend::on_vkDestroyBuffer");
    guard_.destroy(VulkanDestroyKind::buffer, number(device), 0u, number(buffer));
}

void VulkanBackend::on_vkDestroyImage(VkDevice device, VkImage image, const VkAllocationCallbacks *)
{
    AV_SCOPE("VulkanBackend::on_vkDestroyImage");
    const uint64_t dedicated = dedicated_.take_dedicated(number(image));
    guard_.destroy(VulkanDestroyKind::image, number(device), 0u, number(image));
    if (dedicated != 0u) guard_.destroy(VulkanDestroyKind::imported_memory, number(device), 0u, dedicated);
}

void VulkanBackend::on_vkDestroyImageView(VkDevice device, VkImageView imageView, const VkAllocationCallbacks *)
{
    AV_SCOPE("VulkanBackend::on_vkDestroyImageView");
    guard_.destroy(VulkanDestroyKind::image_view, number(device), 0u, number(imageView));
}

void VulkanBackend::on_vkDestroyBufferView(VkDevice device, VkBufferView bufferView, const VkAllocationCallbacks *)
{
    AV_SCOPE("VulkanBackend::on_vkDestroyBufferView");
    guard_.destroy(VulkanDestroyKind::buffer_view, number(device), 0u, number(bufferView));
}

void VulkanBackend::on_vkDestroySampler(VkDevice device, VkSampler sampler, const VkAllocationCallbacks *)
{
    AV_SCOPE("VulkanBackend::on_vkDestroySampler");
    guard_.destroy(VulkanDestroyKind::sampler, number(device), 0u, number(sampler));
}

void VulkanBackend::on_vkDestroyPipeline(VkDevice device, VkPipeline pipeline, const VkAllocationCallbacks *)
{
    AV_SCOPE("VulkanBackend::on_vkDestroyPipeline");
    guard_.destroy(VulkanDestroyKind::pipeline, number(device), 0u, number(pipeline));
}

void VulkanBackend::on_vkDestroyPipelineLayout(VkDevice device, VkPipelineLayout pipelineLayout,
                                               const VkAllocationCallbacks *)
{
    AV_SCOPE("VulkanBackend::on_vkDestroyPipelineLayout");
    guard_.destroy(VulkanDestroyKind::pipeline_layout, number(device), 0u, number(pipelineLayout));
}

void VulkanBackend::on_vkDestroyRenderPass(VkDevice device, VkRenderPass renderPass, const VkAllocationCallbacks *)
{
    AV_SCOPE("VulkanBackend::on_vkDestroyRenderPass");
    guard_.destroy(VulkanDestroyKind::render_pass, number(device), 0u, number(renderPass));
}

void VulkanBackend::on_vkDestroyFramebuffer(VkDevice device, VkFramebuffer framebuffer, const VkAllocationCallbacks *)
{
    AV_SCOPE("VulkanBackend::on_vkDestroyFramebuffer");
    guard_.destroy(VulkanDestroyKind::framebuffer, number(device), 0u, number(framebuffer));
}

void VulkanBackend::on_vkDestroyQueryPool(VkDevice device, VkQueryPool queryPool, const VkAllocationCallbacks *)
{
    AV_SCOPE("VulkanBackend::on_vkDestroyQueryPool");
    guard_.destroy(VulkanDestroyKind::query_pool, number(device), 0u, number(queryPool));
}

VkResult VulkanBackend::on_vkCreateAccelerationStructureKHR(VkDevice device,
                                                            const VkAccelerationStructureCreateInfoKHR *pCreateInfo,
                                                            const VkAllocationCallbacks *,
                                                            VkAccelerationStructureKHR *pAccelerationStructure)
{
    AV_SCOPE("VulkanBackend::on_vkCreateAccelerationStructureKHR");
    if ((pCreateInfo->createFlags & VK_ACCELERATION_STRUCTURE_CREATE_DEVICE_ADDRESS_CAPTURE_REPLAY_BIT_KHR) != 0u ||
        pCreateInfo->deviceAddress != 0u) {
        lines_.write("an acceleration structure asked to be placed at a captured address; refused");
        return VK_ERROR_FEATURE_NOT_PRESENT;
    }
    uint64_t held = 0u;
    uint64_t end = 0u;
    if (!addresses_.buffer_bytes(number(device), number(pCreateInfo->buffer), held) ||
        __builtin_add_overflow(pCreateInfo->offset, pCreateInfo->size, &end) || end > held) {
        lines_.write("an acceleration structure does not fit inside the buffer it names; refused");
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    const VkResult made =
        RealVulkanDispatch::on_vkCreateAccelerationStructureKHR(device, pCreateInfo, nullptr, pAccelerationStructure);
    if (made != VK_SUCCESS) return made;
    if (addresses_.note_structure(number(device), number(*pAccelerationStructure), pCreateInfo->size) &&
        guard_.depends(number(*pAccelerationStructure), number(pCreateInfo->buffer), number(device)))
        return made;
    RealVulkanDispatch::on_vkDestroyAccelerationStructureKHR(device, *pAccelerationStructure, nullptr);
    *pAccelerationStructure = VK_NULL_HANDLE;
    return VK_ERROR_OUT_OF_HOST_MEMORY;
}

void VulkanBackend::on_vkDestroyAccelerationStructureKHR(VkDevice device, VkAccelerationStructureKHR accelerationStructure,
                                                         const VkAllocationCallbacks *)
{
    AV_SCOPE("VulkanBackend::on_vkDestroyAccelerationStructureKHR");
    guard_.destroy(VulkanDestroyKind::acceleration_structure, number(device), 0u, number(accelerationStructure));
}

void VulkanBackend::on_vkDestroyEvent(VkDevice device, VkEvent event, const VkAllocationCallbacks *)
{
    AV_SCOPE("VulkanBackend::on_vkDestroyEvent");
    parked_.drop_event(number(event));
    host_waits_.forget_event(number(event));
    guard_.destroy(VulkanDestroyKind::event, number(device), 0u, number(event));
}

void VulkanBackend::on_vkDestroyDescriptorSetLayout(VkDevice device, VkDescriptorSetLayout descriptorSetLayout,
                                                    const VkAllocationCallbacks *)
{
    AV_SCOPE("VulkanBackend::on_vkDestroyDescriptorSetLayout");
    guard_.destroy(VulkanDestroyKind::descriptor_set_layout, number(device), 0u, number(descriptorSetLayout));
}

void VulkanBackend::on_vkDestroyDescriptorPool(VkDevice device, VkDescriptorPool descriptorPool,
                                               const VkAllocationCallbacks *)
{
    AV_SCOPE("VulkanBackend::on_vkDestroyDescriptorPool");
    release_children(number(descriptorPool), number(device));
    guard_.orphan_children(number(descriptorPool), number(device));
    guard_.destroy(VulkanDestroyKind::descriptor_pool, number(device), 0u, number(descriptorPool));
}

void VulkanBackend::on_vkDestroyCommandPool(VkDevice device, VkCommandPool commandPool, const VkAllocationCallbacks *)
{
    AV_SCOPE("VulkanBackend::on_vkDestroyCommandPool");
    forget_waits_under(number(commandPool));
    release_children(number(commandPool), number(device));
    guard_.orphan_children(number(commandPool), number(device));
    families_.forget(number(commandPool));
    guard_.destroy(VulkanDestroyKind::command_pool, number(device), 0u, number(commandPool));
}

void VulkanBackend::on_vkFreeCommandBuffers(VkDevice device, VkCommandPool commandPool, uint32_t commandBufferCount,
                                            const VkCommandBuffer *pCommandBuffers)
{
    AV_SCOPE("VulkanBackend::on_vkFreeCommandBuffers");
    for (uint32_t index = 0u; index < commandBufferCount; index++) {
        host_waits_.forget_buffer(number(pCommandBuffers[index]));
        static_cast<void>(guard_.release(number(pCommandBuffers[index]), number(device)));
        guard_.destroy(VulkanDestroyKind::command_buffer, number(device), number(commandPool),
                       number(pCommandBuffers[index]));
    }
}

VkResult VulkanBackend::on_vkFreeDescriptorSets(VkDevice device, VkDescriptorPool descriptorPool,
                                                uint32_t descriptorSetCount, const VkDescriptorSet *pDescriptorSets)
{
    AV_SCOPE("VulkanBackend::on_vkFreeDescriptorSets");
    for (uint32_t index = 0u; index < descriptorSetCount; index++) {
        static_cast<void>(guard_.release(number(pDescriptorSets[index]), number(device)));
        guard_.destroy(VulkanDestroyKind::descriptor_set, number(device), number(descriptorPool),
                       number(pDescriptorSets[index]));
    }
    return VK_SUCCESS;
}

VkResult VulkanBackend::on_vkAllocateDescriptorSets(VkDevice device, const VkDescriptorSetAllocateInfo *pAllocateInfo,
                                                    VkDescriptorSet *pDescriptorSets)
{
    AV_SCOPE("VulkanBackend::on_vkAllocateDescriptorSets");
    if (pAllocateInfo != nullptr && guard_.reset_pending(number(pAllocateInfo->descriptorPool), number(device)))
        return VK_ERROR_OUT_OF_POOL_MEMORY;
    return RealVulkanDispatch::on_vkAllocateDescriptorSets(device, pAllocateInfo, pDescriptorSets);
}

VkResult VulkanBackend::on_vkResetDescriptorPool(VkDevice device, VkDescriptorPool descriptorPool,
                                                 VkDescriptorPoolResetFlags flags)
{
    AV_SCOPE("VulkanBackend::on_vkResetDescriptorPool");
    const uint64_t pool = number(descriptorPool);
    const uint64_t owner = number(device);
    guard_.reap();
    if (guard_.reset_pending(pool, owner)) return VK_SUCCESS;
    uint64_t marks[max_child_marks];
    uint32_t count = 0u;
    const bool counted = children_marks(pool, owner, marks, count);
    if (!(counted ? guard_.quiesce_marks(marks, count, owner) : guard_.quiesce(owner))) {
        uint64_t newest = counted ? 0u : guard_.marked();
        for (uint32_t index = 0u; index < count && counted; index++)
            if (marks[index] > newest) newest = marks[index];
        guard_.defer_pool_reset(pool, owner, newest);
        release_children(pool, owner);
        guard_.orphan_children(pool, owner);
        return VK_SUCCESS;
    }
    release_children(pool, owner);
    const VkResult reset = RealVulkanDispatch::on_vkResetDescriptorPool(device, descriptorPool, flags);
    if (reset == VK_SUCCESS) guard_.orphan_children(pool, owner);
    return reset;
}

VkResult VulkanBackend::on_vkResetCommandPool(VkDevice device, VkCommandPool commandPool,
                                              VkCommandPoolResetFlags flags)
{
    AV_SCOPE("VulkanBackend::on_vkResetCommandPool");
    const uint64_t owner = number(device);
    uint64_t       marks[max_child_marks];
    uint32_t       count = 0u;
    const bool     counted = children_marks(number(commandPool), owner, marks, count);
    if (!(counted ? guard_.quiesce_marks(marks, count, owner) : guard_.quiesce(owner)))
        return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    forget_waits_under(number(commandPool));
    release_children(number(commandPool), owner);
    return RealVulkanDispatch::on_vkResetCommandPool(device, commandPool, flags);
}

VkResult VulkanBackend::on_vkBeginCommandBuffer(VkCommandBuffer commandBuffer, const VkCommandBufferBeginInfo *pBeginInfo)
{
    AV_SCOPE("VulkanBackend::on_vkBeginCommandBuffer");
    if (!reusable(commandBuffer)) return VK_ERROR_OUT_OF_HOST_MEMORY;
    return RealVulkanDispatch::on_vkBeginCommandBuffer(commandBuffer, pBeginInfo);
}

VkResult VulkanBackend::on_vkResetCommandBuffer(VkCommandBuffer commandBuffer, VkCommandBufferResetFlags flags)
{
    AV_SCOPE("VulkanBackend::on_vkResetCommandBuffer");
    if (!reusable(commandBuffer)) return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    return RealVulkanDispatch::on_vkResetCommandBuffer(commandBuffer, flags);
}

bool VulkanBackend::reusable(VkCommandBuffer commandBuffer)
{
    AV_SCOPE("VulkanBackend::reusable");
    const uint64_t buffer = number(commandBuffer);
    const uint64_t device = owning_device(buffer);
    if (!guard_.await_reset(buffer, device) || !guard_.release(buffer, device)) return false;
    host_waits_.forget_buffer(buffer);
    return true;
}

bool VulkanBackend::bind_fits(const char *what, VkDeviceMemory memory, VkDeviceSize offset, VkDeviceSize bytes)
{
    AV_SCOPE("VulkanBackend::bind_fits");
    if (bindable_.fits(number(memory), offset, bytes)) return true;
    LogLine(lines_)
        .text(what)
        .text(" of ")
        .number(bytes)
        .text(" bytes at offset ")
        .number(offset)
        .text(" does not fit its memory; the bind is refused")
        .write();
    return false;
}

bool VulkanBackend::buffer_fits(VkDevice device, VkBuffer buffer, VkDeviceMemory memory, VkDeviceSize offset)
{
    AV_SCOPE("VulkanBackend::buffer_fits");
    VkMemoryRequirements needs = {};
    RealVulkanDispatch::on_vkGetBufferMemoryRequirements(device, buffer, &needs);
    return bind_fits("a buffer", memory, offset, needs.size);
}

bool VulkanBackend::image_fits(VkDevice device, const VkBindImageMemoryInfo &bind)
{
    AV_SCOPE("VulkanBackend::image_fits");
    if (bind.memory == VK_NULL_HANDLE) return true;
    VkImagePlaneMemoryRequirementsInfo plane = {VK_STRUCTURE_TYPE_IMAGE_PLANE_MEMORY_REQUIREMENTS_INFO, nullptr,
                                                VK_IMAGE_ASPECT_COLOR_BIT};
    bool planar = false;
    for (const auto *node = static_cast<const VkBaseInStructure *>(bind.pNext); node != nullptr; node = node->pNext) {
        if (node->sType != VK_STRUCTURE_TYPE_BIND_IMAGE_PLANE_MEMORY_INFO) continue;
        plane.planeAspect = reinterpret_cast<const VkBindImagePlaneMemoryInfo *>(node)->planeAspect;
        planar = true;
    }
    VkMemoryRequirements2 needs = {VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2, nullptr, {}};
    if (planar) {
        const VkImageMemoryRequirementsInfo2 asked = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2, &plane,
                                                      bind.image};
        RealVulkanDispatch::on_vkGetImageMemoryRequirements2(device, &asked, &needs);
    } else {
        RealVulkanDispatch::on_vkGetImageMemoryRequirements(device, bind.image, &needs.memoryRequirements);
    }
    return bind_fits("an image", bind.memory, bind.memoryOffset, needs.memoryRequirements.size);
}

bool VulkanBackend::dedicates(const VkBindImageMemoryInfo &bind) const
{
    AV_SCOPE("VulkanBackend::dedicates");
    return bind.pNext == nullptr && bind.memoryOffset == 0u && dedicated_.payload(number(bind.memory)) >= 0;
}

bool VulkanBackend::dedicate_bind(VkDevice device, const VkBindImageMemoryInfo &bind, uint64_t &used)
{
    AV_SCOPE("VulkanBackend::dedicate_bind");
    GuestDevice *const owner = device_of(number(device));
    if (!dedicates(bind) || owner == nullptr) return false;
    const int      fd = dedicated_.payload(number(bind.memory));
    VkMemoryRequirements needs = {};
    RealVulkanDispatch::on_vkGetImageMemoryRequirements(device, bind.image, &needs);
    const uint32_t types = needs.memoryTypeBits & owner->memory().dmabuf_types(fd);
    uint64_t       made = 0u;
    if (types == 0u || !owner->memory().import_dedicated(fd, dedicated_.payload_bytes(number(bind.memory)),
                                                         owner->memory().import_type(owner->memory().admitted(fd, types)),
                                                         number(bind.image), made))
        return false;
    if (dedicated_.dedicate(number(bind.image), made) != Status::ok) {
        owner->memory().release(made);
        return false;
    }
    if (RealVulkanDispatch::on_vkBindImageMemory(device, bind.image, reinterpret_cast<VkDeviceMemory>(made), 0u) ==
        VK_SUCCESS) {
        used = made;
        return true;
    }
    static_cast<void>(dedicated_.take_dedicated(number(bind.image)));
    owner->memory().release(made);
    return false;
}

VkResult VulkanBackend::on_vkBindBufferMemory(VkDevice device, VkBuffer buffer, VkDeviceMemory memory,
                                              VkDeviceSize memoryOffset)
{
    AV_SCOPE("VulkanBackend::on_vkBindBufferMemory");
    if (!buffer_fits(device, buffer, memory, memoryOffset)) return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    const VkResult bound = RealVulkanDispatch::on_vkBindBufferMemory(device, buffer, memory, memoryOffset);
    if (bound != VK_SUCCESS || guard_.depends(number(buffer), number(memory), number(device))) return bound;
    return VK_ERROR_OUT_OF_HOST_MEMORY;
}

VkResult VulkanBackend::on_vkBindImageMemory(VkDevice device, VkImage image, VkDeviceMemory memory,
                                             VkDeviceSize memoryOffset)
{
    AV_SCOPE("VulkanBackend::on_vkBindImageMemory");
    const VkBindImageMemoryInfo bind = {VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_INFO, nullptr, image, memory, memoryOffset};
    if (!image_fits(device, bind)) return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    uint64_t       used = number(memory);
    const VkResult bound = dedicate_bind(device, bind, used)
                               ? VK_SUCCESS
                               : RealVulkanDispatch::on_vkBindImageMemory(device, image, memory, memoryOffset);
    if (bound != VK_SUCCESS || guard_.depends(number(image), used, number(device))) return bound;
    return VK_ERROR_OUT_OF_HOST_MEMORY;
}

VkResult VulkanBackend::on_vkBindBufferMemory2(VkDevice device, uint32_t bindInfoCount,
                                               const VkBindBufferMemoryInfo *pBindInfos)
{
    AV_SCOPE("VulkanBackend::on_vkBindBufferMemory2");
    for (uint32_t index = 0u; index < bindInfoCount; index++)
        if (!buffer_fits(device, pBindInfos[index].buffer, pBindInfos[index].memory, pBindInfos[index].memoryOffset))
            return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    const VkResult bound = RealVulkanDispatch::on_vkBindBufferMemory2(device, bindInfoCount, pBindInfos);
    if (bound != VK_SUCCESS) return bound;
    for (uint32_t index = 0u; index < bindInfoCount; index++)
        if (!guard_.depends(number(pBindInfos[index].buffer), number(pBindInfos[index].memory), number(device)))
            return VK_ERROR_OUT_OF_HOST_MEMORY;
    return bound;
}

VkResult VulkanBackend::on_vkBindBufferMemory2KHR(VkDevice device, uint32_t bindInfoCount,
                                                  const VkBindBufferMemoryInfo *pBindInfos)
{
    AV_SCOPE("VulkanBackend::on_vkBindBufferMemory2KHR");
    return on_vkBindBufferMemory2(device, bindInfoCount, pBindInfos);
}

VkResult VulkanBackend::on_vkBindImageMemory2(VkDevice device, uint32_t bindInfoCount,
                                              const VkBindImageMemoryInfo *pBindInfos)
{
    AV_SCOPE("VulkanBackend::on_vkBindImageMemory2");
    bool dedicated = false;
    for (uint32_t index = 0u; index < bindInfoCount; index++) {
        if (!image_fits(device, pBindInfos[index])) return VK_ERROR_OUT_OF_DEVICE_MEMORY;
        dedicated = dedicated || dedicates(pBindInfos[index]);
    }
    if (dedicated) {
        for (uint32_t index = 0u; index < bindInfoCount; index++) {
            uint64_t       used = number(pBindInfos[index].memory);
            const VkResult one = dedicate_bind(device, pBindInfos[index], used)
                                     ? VK_SUCCESS
                                     : RealVulkanDispatch::on_vkBindImageMemory2(device, 1u, &pBindInfos[index]);
            if (one != VK_SUCCESS) return one;
            if (!guard_.depends(number(pBindInfos[index].image), used, number(device))) return VK_ERROR_OUT_OF_HOST_MEMORY;
        }
        return VK_SUCCESS;
    }
    const VkResult bound = RealVulkanDispatch::on_vkBindImageMemory2(device, bindInfoCount, pBindInfos);
    if (bound != VK_SUCCESS) return bound;
    for (uint32_t index = 0u; index < bindInfoCount; index++)
        if (!guard_.depends(number(pBindInfos[index].image), number(pBindInfos[index].memory), number(device)))
            return VK_ERROR_OUT_OF_HOST_MEMORY;
    return bound;
}

VkResult VulkanBackend::on_vkBindImageMemory2KHR(VkDevice device, uint32_t bindInfoCount,
                                                 const VkBindImageMemoryInfo *pBindInfos)
{
    AV_SCOPE("VulkanBackend::on_vkBindImageMemory2KHR");
    return on_vkBindImageMemory2(device, bindInfoCount, pBindInfos);
}

VkResult VulkanBackend::on_vkCreateImageView(VkDevice device, const VkImageViewCreateInfo *pCreateInfo,
                                             const VkAllocationCallbacks *, VkImageView *pView)
{
    AV_SCOPE("VulkanBackend::on_vkCreateImageView");
    const VkResult made = RealVulkanDispatch::on_vkCreateImageView(device, pCreateInfo, nullptr, pView);
    if (made != VK_SUCCESS || guard_.depends(number(*pView), number(pCreateInfo->image), number(device))) return made;
    RealVulkanDispatch::on_vkDestroyImageView(device, *pView, nullptr);
    *pView = VK_NULL_HANDLE;
    return VK_ERROR_OUT_OF_HOST_MEMORY;
}

VkResult VulkanBackend::on_vkCreateBufferView(VkDevice device, const VkBufferViewCreateInfo *pCreateInfo,
                                              const VkAllocationCallbacks *, VkBufferView *pView)
{
    AV_SCOPE("VulkanBackend::on_vkCreateBufferView");
    const VkResult made = RealVulkanDispatch::on_vkCreateBufferView(device, pCreateInfo, nullptr, pView);
    if (made != VK_SUCCESS || guard_.depends(number(*pView), number(pCreateInfo->buffer), number(device))) return made;
    RealVulkanDispatch::on_vkDestroyBufferView(device, *pView, nullptr);
    *pView = VK_NULL_HANDLE;
    return VK_ERROR_OUT_OF_HOST_MEMORY;
}

VkResult VulkanBackend::on_vkCreateFramebuffer(VkDevice device, const VkFramebufferCreateInfo *pCreateInfo,
                                               const VkAllocationCallbacks *, VkFramebuffer *pFramebuffer)
{
    AV_SCOPE("VulkanBackend::on_vkCreateFramebuffer");
    const VkResult made = RealVulkanDispatch::on_vkCreateFramebuffer(device, pCreateInfo, nullptr, pFramebuffer);
    if (made != VK_SUCCESS || (pCreateInfo->flags & VK_FRAMEBUFFER_CREATE_IMAGELESS_BIT) != 0u) return made;
    bool held = true;
    for (uint32_t index = 0u; index < pCreateInfo->attachmentCount && held; index++)
        held = guard_.depends(number(*pFramebuffer), number(pCreateInfo->pAttachments[index]), number(device));
    if (held) return made;
    RealVulkanDispatch::on_vkDestroyFramebuffer(device, *pFramebuffer, nullptr);
    *pFramebuffer = VK_NULL_HANDLE;
    return VK_ERROR_OUT_OF_HOST_MEMORY;
}

void VulkanBackend::on_vkUpdateDescriptorSets(VkDevice device, uint32_t descriptorWriteCount,
                                              const VkWriteDescriptorSet *pDescriptorWrites,
                                              uint32_t descriptorCopyCount, const VkCopyDescriptorSet *pDescriptorCopies)
{
    AV_SCOPE("VulkanBackend::on_vkUpdateDescriptorSets");
    const bool held = guard_.written(pDescriptorWrites, descriptorWriteCount, number(device)) &&
                      guard_.copied(pDescriptorCopies, descriptorCopyCount, number(device));
    if (!held && !guard_.quiesce(number(device))) return;
    RealVulkanDispatch::on_vkUpdateDescriptorSets(device, descriptorWriteCount, pDescriptorWrites,
                                                  descriptorCopyCount, pDescriptorCopies);
}

bool VulkanBackend::resolved(const VulkanTemplates::Reference &reference, uint64_t &handle) const
{
    AV_SCOPE("VulkanBackend::resolved");
    handle = 0u;
    return !reference.named_ || handle_for(reference.kind_, reference.handle_, handle);
}

bool VulkanBackend::template_named(VkDescriptorUpdateTemplate made, const void *data, uint64_t set, uint64_t device)
{
    AV_SCOPE("VulkanBackend::template_named");
    const uint64_t number_of = number(made);
    if (handles_.find(number_of, VulkanObjectKind::descriptor_template) == nullptr) {
        lines_.note("a descriptor update refused, the template is not known, template", number_of);
        return false;
    }
    bool held = true;
    const bool named = templates_.each(number_of, data, template_bytes(made), [&](const VulkanTemplates::Descriptor &at) {
        uint64_t first = 0u;
        uint64_t second = 0u;
        if (!resolved(at.first_, first) || !resolved(at.second_, second)) return false;
        if (set != 0u && !guard_.bound(set, at.binding_, at.element_, first, second, device)) held = false;
        return true;
    });
    if (!named) lines_.note("a descriptor update refused, a handle in its data is not known, template", number_of);
    return named && (held || guard_.quiesce(device));
}

void VulkanBackend::on_vkUpdateDescriptorSetWithTemplate(VkDevice device, VkDescriptorSet descriptorSet,
                                                         VkDescriptorUpdateTemplate descriptorUpdateTemplate,
                                                         const void *pData)
{
    AV_SCOPE("VulkanBackend::on_vkUpdateDescriptorSetWithTemplate");
    if (!template_named(descriptorUpdateTemplate, pData, number(descriptorSet), number(device))) return;
    RealVulkanDispatch::on_vkUpdateDescriptorSetWithTemplate(device, descriptorSet, descriptorUpdateTemplate, pData);
}

void VulkanBackend::on_vkUpdateDescriptorSetWithTemplateKHR(VkDevice device, VkDescriptorSet descriptorSet,
                                                            VkDescriptorUpdateTemplate descriptorUpdateTemplate,
                                                            const void *pData)
{
    AV_SCOPE("VulkanBackend::on_vkUpdateDescriptorSetWithTemplateKHR");
    if (!template_named(descriptorUpdateTemplate, pData, number(descriptorSet), number(device))) return;
    RealVulkanDispatch::on_vkUpdateDescriptorSetWithTemplateKHR(device, descriptorSet, descriptorUpdateTemplate, pData);
}

void VulkanBackend::on_vkCmdPushDescriptorSetWithTemplateKHR(VkCommandBuffer commandBuffer,
                                                             VkDescriptorUpdateTemplate descriptorUpdateTemplate,
                                                             VkPipelineLayout layout, uint32_t set, const void *pData)
{
    AV_SCOPE("VulkanBackend::on_vkCmdPushDescriptorSetWithTemplateKHR");
    if (!template_named(descriptorUpdateTemplate, pData, 0u, 0u)) return;
    RealVulkanDispatch::on_vkCmdPushDescriptorSetWithTemplateKHR(commandBuffer, descriptorUpdateTemplate, layout, set, pData);
}

void VulkanBackend::on_vkCmdPushDescriptorSetWithTemplate2KHR(
    VkCommandBuffer commandBuffer, const VkPushDescriptorSetWithTemplateInfo *pPushDescriptorSetWithTemplateInfo)
{
    AV_SCOPE("VulkanBackend::on_vkCmdPushDescriptorSetWithTemplate2KHR");
    if (!template_named(pPushDescriptorSetWithTemplateInfo->descriptorUpdateTemplate,
                        pPushDescriptorSetWithTemplateInfo->pData, 0u, 0u))
        return;
    RealVulkanDispatch::on_vkCmdPushDescriptorSetWithTemplate2KHR(commandBuffer, pPushDescriptorSetWithTemplateInfo);
}

}
