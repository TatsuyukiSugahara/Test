#pragma once
#include "Graphics/Vulkan/VulkanCommon.h"

namespace aq
{
	namespace graphics
	{
		// ── Vulkan パイプラインレイアウト (Phase 1b) ──
		// D3D12 の静的ルートシグネチャに相当。エンジンのバインドモデル (HLSL register) を
		// 単一 DescriptorSet (set=0) に集約する。register→binding は設計 §5.1 のシフト規約:
		//   b# → binding 0+#  (UNIFORM_BUFFER)
		//   t# → binding 16+# (SAMPLED_IMAGE)   ※ Phase 2 で書き込み
		//   s# → binding 32+# (SAMPLER)         ※ Phase 2 で書き込み
		// 使用しない binding は「シェーダが静的に参照しなければ未書き込みで可」(Vulkan 仕様)。
		class VulkanPipelineLayout
		{
		public:
			// register シフト基準 (設計 §5.1)
			static constexpr uint32_t CBV_BINDING_BASE     = 0;   // b0..b5
			static constexpr uint32_t CBV_COUNT            = 6;
			static constexpr uint32_t SRV_BINDING_BASE     = 16;  // t0..t11
			static constexpr uint32_t SRV_COUNT            = 12;
			static constexpr uint32_t SAMPLER_BINDING_BASE = 32;  // s0..s1
			static constexpr uint32_t SAMPLER_COUNT        = 2;
			static constexpr uint32_t UAV_BINDING_BASE     = 48;  // u0..u7 (compute, STORAGE_IMAGE)
			static constexpr uint32_t UAV_COUNT            = 8;

			bool Create(VkDevice device);
			void Destroy(VkDevice device);

			// グラフィクス用
			VkDescriptorSetLayout GetSetLayout() const      { return setLayout_; }
			VkPipelineLayout      GetPipelineLayout() const { return pipelineLayout_; }
			// compute 用 (UAV binding を含む・stage=COMPUTE)
			VkDescriptorSetLayout GetComputeSetLayout() const      { return computeSetLayout_; }
			VkPipelineLayout      GetComputePipelineLayout() const { return computePipelineLayout_; }

			static uint32_t CbvBinding(uint32_t slot)     { return CBV_BINDING_BASE + slot; }
			static uint32_t SrvBinding(uint32_t slot)     { return SRV_BINDING_BASE + slot; }
			static uint32_t SamplerBinding(uint32_t slot) { return SAMPLER_BINDING_BASE + slot; }
			static uint32_t UavBinding(uint32_t slot)     { return UAV_BINDING_BASE + slot; }

		private:
			VkDescriptorSetLayout setLayout_      = VK_NULL_HANDLE;
			VkPipelineLayout      pipelineLayout_ = VK_NULL_HANDLE;
			VkDescriptorSetLayout computeSetLayout_      = VK_NULL_HANDLE;
			VkPipelineLayout      computePipelineLayout_ = VK_NULL_HANDLE;
		};
	}
}
