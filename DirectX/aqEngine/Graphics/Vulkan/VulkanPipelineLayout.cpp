#include "aq.h"
#ifdef ENGINE_GRAPHICS_VULKAN
#include "Graphics/Vulkan/VulkanPipelineLayout.h"
#include <vector>

namespace aq
{
	namespace graphics
	{
		bool VulkanPipelineLayout::Create(VkDevice device)
		{
			std::vector<VkDescriptorSetLayoutBinding> bindings;
			bindings.reserve(CBV_COUNT + SRV_COUNT + SAMPLER_COUNT);

			auto add = [&](uint32_t binding, VkDescriptorType type)
			{
				VkDescriptorSetLayoutBinding b{};
				b.binding         = binding;
				b.descriptorType  = type;
				b.descriptorCount = 1;
				b.stageFlags      = VK_SHADER_STAGE_ALL_GRAPHICS;  // b0 は VS/PS 双方で使う箇所がある
				bindings.push_back(b);
			};

			for (uint32_t i = 0; i < CBV_COUNT; ++i)     add(CbvBinding(i),     VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
			for (uint32_t i = 0; i < SRV_COUNT; ++i)     add(SrvBinding(i),     VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
			for (uint32_t i = 0; i < SAMPLER_COUNT; ++i) add(SamplerBinding(i), VK_DESCRIPTOR_TYPE_SAMPLER);

			VkDescriptorSetLayoutCreateInfo slci{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
			slci.bindingCount = (uint32_t)bindings.size();
			slci.pBindings    = bindings.data();
			if (!VK_VERIFY(vkCreateDescriptorSetLayout(device, &slci, nullptr, &setLayout_))) return false;

			VkPipelineLayoutCreateInfo plci{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
			plci.setLayoutCount = 1;
			plci.pSetLayouts    = &setLayout_;
			if (!VK_VERIFY(vkCreatePipelineLayout(device, &plci, nullptr, &pipelineLayout_))) return false;

			// ── compute 用 (UAV を追加・stage=COMPUTE) ──
			std::vector<VkDescriptorSetLayoutBinding> cbind;
			auto addC = [&](uint32_t binding, VkDescriptorType type)
			{
				VkDescriptorSetLayoutBinding b{};
				b.binding = binding; b.descriptorType = type; b.descriptorCount = 1;
				b.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
				cbind.push_back(b);
			};
			for (uint32_t i = 0; i < CBV_COUNT; ++i)     addC(CbvBinding(i),     VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
			for (uint32_t i = 0; i < SRV_COUNT; ++i)     addC(SrvBinding(i),     VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
			for (uint32_t i = 0; i < SAMPLER_COUNT; ++i) addC(SamplerBinding(i), VK_DESCRIPTOR_TYPE_SAMPLER);
			for (uint32_t i = 0; i < UAV_COUNT; ++i)     addC(UavBinding(i),     VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);

			VkDescriptorSetLayoutCreateInfo cslci{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
			cslci.bindingCount = (uint32_t)cbind.size();
			cslci.pBindings    = cbind.data();
			if (!VK_VERIFY(vkCreateDescriptorSetLayout(device, &cslci, nullptr, &computeSetLayout_))) return false;

			VkPipelineLayoutCreateInfo cplci{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
			cplci.setLayoutCount = 1;
			cplci.pSetLayouts    = &computeSetLayout_;
			return VK_VERIFY(vkCreatePipelineLayout(device, &cplci, nullptr, &computePipelineLayout_));
		}

		void VulkanPipelineLayout::Destroy(VkDevice device)
		{
			if (computePipelineLayout_) { vkDestroyPipelineLayout(device, computePipelineLayout_, nullptr); computePipelineLayout_ = VK_NULL_HANDLE; }
			if (computeSetLayout_)      { vkDestroyDescriptorSetLayout(device, computeSetLayout_, nullptr); computeSetLayout_ = VK_NULL_HANDLE; }
			if (pipelineLayout_) { vkDestroyPipelineLayout(device, pipelineLayout_, nullptr); pipelineLayout_ = VK_NULL_HANDLE; }
			if (setLayout_)      { vkDestroyDescriptorSetLayout(device, setLayout_, nullptr); setLayout_ = VK_NULL_HANDLE; }
		}
	}
}
#endif // ENGINE_GRAPHICS_VULKAN
