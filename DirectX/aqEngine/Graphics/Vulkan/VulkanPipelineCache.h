#pragma once
#include "Graphics/Vulkan/VulkanCommon.h"
#include "Graphics/IRenderContextImpl.h"  // BlendMode / DepthMode
#include <unordered_map>
#include <cstring>

namespace aq
{
	namespace graphics
	{
		class VulkanShader;

		// ── Vulkan パイプラインキャッシュ (Phase 1b) ──
		// D3D12 の PipelineStateCache に相当。(VS,PS,topo,blend,depth,RTフォーマット) を
		// 1 つの VkPipeline に集約し、dynamic rendering (VkPipelineRenderingCreateInfo) で生成する。
		// 遅延生成: Draw flush 時にキーを組み、未ヒットなら vkCreateGraphicsPipelines。

		struct VulkanPipelineKey
		{
			VkShaderModule    vs       = VK_NULL_HANDLE;
			VkShaderModule    ps       = VK_NULL_HANDLE;
			uint32_t          topology = 0;   // VkPrimitiveTopology
			uint8_t           blend    = 0;   // BlendMode
			uint8_t           depth    = 0;   // DepthMode
			uint8_t           rtCount  = 0;
			uint8_t           _pad     = 0;
			uint32_t          vertexStride = 0;  // 実際の VB stride (リフレクション値でなく)
			VkFormat          rtFormats[8] = {};
			VkFormat          dsFormat = VK_FORMAT_UNDEFINED;

			bool operator==(const VulkanPipelineKey& o) const
			{
				return std::memcmp(this, &o, sizeof(VulkanPipelineKey)) == 0;
			}
		};

		struct VulkanPipelineKeyHash
		{
			size_t operator()(const VulkanPipelineKey& k) const
			{
				const auto* p = reinterpret_cast<const uint8_t*>(&k);
				size_t h = 1469598103934665603ull;  // FNV-1a
				for (size_t i = 0; i < sizeof(VulkanPipelineKey); ++i) { h ^= p[i]; h *= 1099511628211ull; }
				return h;
			}
		};

		class VulkanPipelineCache
		{
		public:
			void Destroy(VkDevice device);

			// キー未ヒットなら生成して返す。vsShader は頂点入力レイアウト取得に使う。
			VkPipeline GetOrCreate(VkDevice device, VkPipelineLayout layout,
			                       const VulkanPipelineKey& key,
			                       const VulkanShader* vsShader);

			// compute パイプライン (CS module をキーに)。
			VkPipeline GetOrCreateCompute(VkDevice device, VkPipelineLayout layout, VkShaderModule cs);

		private:
			std::unordered_map<VulkanPipelineKey, VkPipeline, VulkanPipelineKeyHash> map_;
			std::unordered_map<VkShaderModule, VkPipeline> computeMap_;
		};

		// ── 列挙変換ヘルパ (RenderContext からも使う) ──
		VkPrimitiveTopology ToVkTopology(PrimitiveTopology topo);
	}
}
