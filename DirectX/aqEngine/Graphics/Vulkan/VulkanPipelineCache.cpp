#include "aq.h"
#ifdef ENGINE_GRAPHICS_VULKAN
#include "Graphics/Vulkan/VulkanPipelineCache.h"
#include "Graphics/Vulkan/VulkanShader.h"

namespace aq
{
	namespace graphics
	{
		VkPrimitiveTopology ToVkTopology(PrimitiveTopology topo)
		{
			switch (topo)
			{
			case PrimitiveTopology::TriangleList:  return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
			case PrimitiveTopology::TriangleStrip: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
			case PrimitiveTopology::LineList:      return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
			case PrimitiveTopology::LineStrip:     return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
			case PrimitiveTopology::PointList:     return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
			}
			return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		}

		namespace
		{
			// BlendMode → カラーブレンドアタッチメント (D3D11/D3D12 の対応表を踏襲)。
			VkPipelineColorBlendAttachmentState ToBlendAttachment(BlendMode mode)
			{
				VkPipelineColorBlendAttachmentState a{};
				a.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
				                   VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
				a.blendEnable = VK_TRUE;
				a.colorBlendOp = VK_BLEND_OP_ADD;
				a.alphaBlendOp = VK_BLEND_OP_ADD;
				switch (mode)
				{
				case BlendMode::Opaque:
					a.blendEnable = VK_FALSE;
					break;
				case BlendMode::AlphaBlend:
					a.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
					a.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
					a.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
					a.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
					break;
				case BlendMode::Additive:
					a.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
					a.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
					a.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
					a.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
					break;
				case BlendMode::Premultiplied:
					a.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
					a.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
					a.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
					a.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
					break;
				case BlendMode::DecalColor:
					// RGB のみアルファ合成・アルファ ch は書き込みマスク (GBuffer0.a=metallic 保護)
					a.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
					a.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
					a.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
					a.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
					a.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT;
					break;
				}
				return a;
			}

			VkPipelineDepthStencilStateCreateInfo ToDepthState(DepthMode mode)
			{
				VkPipelineDepthStencilStateCreateInfo ds{ VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
				ds.depthCompareOp = VK_COMPARE_OP_LESS;
				switch (mode)
				{
				case DepthMode::ReadWrite: ds.depthTestEnable = VK_TRUE;  ds.depthWriteEnable = VK_TRUE;  break;
				case DepthMode::ReadOnly:  ds.depthTestEnable = VK_TRUE;  ds.depthWriteEnable = VK_FALSE; break;
				case DepthMode::Disabled:  ds.depthTestEnable = VK_FALSE; ds.depthWriteEnable = VK_FALSE; break;
				}
				return ds;
			}
		}

		VkPipeline VulkanPipelineCache::GetOrCreate(VkDevice device, VkPipelineLayout layout,
		                                            const VulkanPipelineKey& key, const VulkanShader* vsShader)
		{
			auto it = map_.find(key);
			if (it != map_.end()) return it->second;

			// シェーダステージ (PS 無し = 深度のみシャドウパス)
			VkPipelineShaderStageCreateInfo stages[2]{};
			stages[0] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
			stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
			stages[0].module = key.vs;
			stages[0].pName  = "main";
			uint32_t stageCount = 1;
			if (key.ps != VK_NULL_HANDLE)
			{
				stages[1] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
				stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
				stages[1].module = key.ps;
				stages[1].pName  = "main";
				stageCount = 2;
			}

			// 頂点入力 (属性=VS リフレクション由来、stride=実際の VB stride)
			const VkVertexInputAttributeDescription* attrs = nullptr;
			uint32_t attrCount = 0, reflStride = 0;
			if (vsShader) vsShader->GetInputLayout(attrs, attrCount, reflStride);
			const uint32_t stride = key.vertexStride ? key.vertexStride : reflStride;

			VkVertexInputBindingDescription binding{ 0, stride, VK_VERTEX_INPUT_RATE_VERTEX };
			VkPipelineVertexInputStateCreateInfo vi{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
			if (stride > 0 && attrCount > 0)
			{
				vi.vertexBindingDescriptionCount   = 1;
				vi.pVertexBindingDescriptions      = &binding;
				vi.vertexAttributeDescriptionCount = attrCount;
				vi.pVertexAttributeDescriptions    = attrs;
			}

			VkPipelineInputAssemblyStateCreateInfo ia{ VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
			ia.topology = (VkPrimitiveTopology)key.topology;

			VkPipelineViewportStateCreateInfo vp{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
			vp.viewportCount = 1;
			vp.scissorCount  = 1;

			VkPipelineRasterizationStateCreateInfo rs{ VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
			rs.polygonMode = VK_POLYGON_MODE_FILL;
			rs.cullMode    = VK_CULL_MODE_NONE;            // Phase 1b は安全側 (winding 検証後に BACK へ)
			rs.frontFace   = VK_FRONT_FACE_CLOCKWISE;      // 負ビューポート Y-flip と D3D 既定に整合
			rs.lineWidth   = 1.0f;

			VkPipelineMultisampleStateCreateInfo ms{ VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
			ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

			VkPipelineDepthStencilStateCreateInfo ds = ToDepthState((DepthMode)key.depth);
			// 深度 attachment が無い (dsFormat=UNDEFINED) パスでは深度テスト/書込を強制無効化する。
			if (key.dsFormat == VK_FORMAT_UNDEFINED)
			{
				ds.depthTestEnable  = VK_FALSE;
				ds.depthWriteEnable = VK_FALSE;
			}

			std::vector<VkPipelineColorBlendAttachmentState> blends(key.rtCount, ToBlendAttachment((BlendMode)key.blend));
			VkPipelineColorBlendStateCreateInfo cb{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
			cb.attachmentCount = key.rtCount;
			cb.pAttachments    = blends.empty() ? nullptr : blends.data();

			VkDynamicState dynStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
			VkPipelineDynamicStateCreateInfo dyn{ VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
			dyn.dynamicStateCount = (uint32_t)std::size(dynStates);
			dyn.pDynamicStates    = dynStates;

			// dynamic rendering: VkRenderPass を作らずフォーマットを直接指定
			VkPipelineRenderingCreateInfo rendering{ VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
			rendering.colorAttachmentCount    = key.rtCount;
			rendering.pColorAttachmentFormats = key.rtCount ? key.rtFormats : nullptr;
			rendering.depthAttachmentFormat   = key.dsFormat;

			VkGraphicsPipelineCreateInfo pci{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
			pci.pNext               = &rendering;
			pci.stageCount          = stageCount;
			pci.pStages             = stages;
			pci.pVertexInputState   = &vi;
			pci.pInputAssemblyState = &ia;
			pci.pViewportState      = &vp;
			pci.pRasterizationState = &rs;
			pci.pMultisampleState   = &ms;
			pci.pDepthStencilState  = &ds;
			pci.pColorBlendState    = &cb;
			pci.pDynamicState       = &dyn;
			pci.layout              = layout;

			VkPipeline pipeline = VK_NULL_HANDLE;
			if (!VK_VERIFY(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pci, nullptr, &pipeline)))
				return VK_NULL_HANDLE;

			map_[key] = pipeline;
			return pipeline;
		}

		VkPipeline VulkanPipelineCache::GetOrCreateCompute(VkDevice device, VkPipelineLayout layout, VkShaderModule cs)
		{
			auto it = computeMap_.find(cs);
			if (it != computeMap_.end()) return it->second;

			VkPipelineShaderStageCreateInfo stage{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
			stage.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
			stage.module = cs;
			stage.pName  = "main";

			VkComputePipelineCreateInfo pci{ VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
			pci.stage  = stage;
			pci.layout = layout;

			VkPipeline pipeline = VK_NULL_HANDLE;
			if (!VK_VERIFY(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pci, nullptr, &pipeline)))
				return VK_NULL_HANDLE;
			computeMap_[cs] = pipeline;
			return pipeline;
		}

		void VulkanPipelineCache::Destroy(VkDevice device)
		{
			for (auto& kv : map_) if (kv.second) vkDestroyPipeline(device, kv.second, nullptr);
			map_.clear();
			for (auto& kv : computeMap_) if (kv.second) vkDestroyPipeline(device, kv.second, nullptr);
			computeMap_.clear();
		}
	}
}
#endif // ENGINE_GRAPHICS_VULKAN
