#include "aq.h"
#if defined(ENGINE_GRAPHICS_VULKAN) && defined(AQ_IMGUI)
#include "Graphics/Vulkan/VulkanImGui.h"
#include "Graphics/Vulkan/VulkanGraphicsDeviceImpl.h"
#include "Graphics/Vulkan/VulkanShader.h"
#include "Graphics/Vulkan/VulkanResources.h"
#include "Graphics/Vulkan/VulkanBuffers.h"
#include "Graphics/Vulkan/VulkanPipelineLayout.h"
#include "Graphics/GraphicsDevice.h"
#include <imgui/imgui.h>
#include <memory>
#include <vector>

namespace aq
{
	namespace graphics
	{
		namespace VulkanImGui
		{
			namespace
			{
				VulkanGraphicsDeviceImpl* g_dev = nullptr;
				VkPipeline   g_pipeline = VK_NULL_HANDLE;
				std::unique_ptr<IShader>             g_vs, g_ps;
				std::unique_ptr<IShaderResourceView> g_font;      // VulkanTexture
				std::unique_ptr<ISamplerState>       g_sampler;   // VulkanSampler
				std::unique_ptr<IConstantBuffer>     g_projCB;    // float4 scale/translate
				std::unique_ptr<IVertexBuffer>       g_vb;
				std::unique_ptr<IIndexBuffer>        g_ib;
				uint32_t g_vbCap = 0, g_ibCap = 0;  // 頂点数 / インデックス数
				VkImageView g_fontView = VK_NULL_HANDLE;

				VkShaderModule ModuleOf(IShader* s) { return s ? static_cast<VulkanShader*>(s)->GetModule() : VK_NULL_HANDLE; }

				bool CreatePipeline()
				{
					VkDevice dev = g_dev->GetDevice();
					VkPipelineShaderStageCreateInfo stages[2]{};
					stages[0] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
					stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;   stages[0].module = ModuleOf(g_vs.get()); stages[0].pName = "main";
					stages[1] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
					stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; stages[1].module = ModuleOf(g_ps.get()); stages[1].pName = "main";
					if (!stages[0].module || !stages[1].module) return false;

					// 頂点入力: ImDrawVert (pos float2, uv float2, col RGBA8)。stride=20。
					VkVertexInputBindingDescription bind{ 0, (uint32_t)sizeof(ImDrawVert), VK_VERTEX_INPUT_RATE_VERTEX };
					VkVertexInputAttributeDescription attrs[3]{};
					attrs[0] = { 0, 0, VK_FORMAT_R32G32_SFLOAT,  (uint32_t)offsetof(ImDrawVert, pos) };
					attrs[1] = { 1, 0, VK_FORMAT_R32G32_SFLOAT,  (uint32_t)offsetof(ImDrawVert, uv)  };
					attrs[2] = { 2, 0, VK_FORMAT_R8G8B8A8_UNORM, (uint32_t)offsetof(ImDrawVert, col) };
					VkPipelineVertexInputStateCreateInfo vi{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
					vi.vertexBindingDescriptionCount = 1; vi.pVertexBindingDescriptions = &bind;
					vi.vertexAttributeDescriptionCount = 3; vi.pVertexAttributeDescriptions = attrs;

					VkPipelineInputAssemblyStateCreateInfo ia{ VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
					ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
					VkPipelineViewportStateCreateInfo vp{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
					vp.viewportCount = 1; vp.scissorCount = 1;
					VkPipelineRasterizationStateCreateInfo rs{ VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
					rs.polygonMode = VK_POLYGON_MODE_FILL; rs.cullMode = VK_CULL_MODE_NONE; rs.frontFace = VK_FRONT_FACE_CLOCKWISE; rs.lineWidth = 1.0f;
					VkPipelineMultisampleStateCreateInfo ms{ VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
					ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
					VkPipelineDepthStencilStateCreateInfo ds{ VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };

					VkPipelineColorBlendAttachmentState cba{};
					cba.blendEnable = VK_TRUE;
					cba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
					cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
					cba.colorBlendOp = VK_BLEND_OP_ADD;
					cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
					cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
					cba.alphaBlendOp = VK_BLEND_OP_ADD;
					cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
					VkPipelineColorBlendStateCreateInfo cb{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
					cb.attachmentCount = 1; cb.pAttachments = &cba;

					VkDynamicState dyns[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
					VkPipelineDynamicStateCreateInfo dyn{ VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
					dyn.dynamicStateCount = 2; dyn.pDynamicStates = dyns;

					VkFormat colorFmt = g_dev->GetSwapchainFormat();
					VkPipelineRenderingCreateInfo rendering{ VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
					rendering.colorAttachmentCount = 1; rendering.pColorAttachmentFormats = &colorFmt;

					VkGraphicsPipelineCreateInfo pci{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
					pci.pNext = &rendering;
					pci.stageCount = 2; pci.pStages = stages;
					pci.pVertexInputState = &vi; pci.pInputAssemblyState = &ia; pci.pViewportState = &vp;
					pci.pRasterizationState = &rs; pci.pMultisampleState = &ms; pci.pDepthStencilState = &ds;
					pci.pColorBlendState = &cb; pci.pDynamicState = &dyn;
					pci.layout = g_dev->GetPipelineLayout()->GetPipelineLayout();
					return VK_VERIFY(vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &pci, nullptr, &g_pipeline));
				}

				bool CreateFontTexture()
				{
					ImGuiIO& io = ImGui::GetIO();
					unsigned char* pixels = nullptr; int w = 0, h = 0;
					io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
					if (!pixels || w <= 0 || h <= 0) return false;

					Texture2DDesc td; td.width = (uint32_t)w; td.height = (uint32_t)h; td.format = PixelFormat::R8G8B8A8_Unorm;
					ImageData id; id.pixels = pixels; id.rowPitch = (uint32_t)w * 4; id.slicePitch = id.rowPitch * (uint32_t)h;
					g_font = g_dev->CreateTexture2D(td, id);
					if (!g_font) return false;
					g_fontView = static_cast<VulkanSRV*>(g_font->GetNativeHandle())->GetImageView();
					io.Fonts->SetTexID((ImTextureID)(uintptr_t)g_fontView);
					return g_fontView != VK_NULL_HANDLE;
				}

				bool EnsureBuffers(uint32_t vtxCount, uint32_t idxCount)
				{
					if (vtxCount > g_vbCap)
					{
						g_vbCap = vtxCount + 5000;
						g_vb = g_dev->CreateDynamicVertexBuffer(g_vbCap, (uint32_t)sizeof(ImDrawVert), nullptr);
						if (!g_vb) { g_vbCap = 0; return false; }
					}
					if (idxCount > g_ibCap)
					{
						g_ibCap = idxCount + 10000;
						g_ib = g_dev->CreateDynamicIndexBuffer(g_ibCap, sizeof(ImDrawIdx) == 2 ? IndexFormat::UInt16 : IndexFormat::UInt32, nullptr);
						if (!g_ib) { g_ibCap = 0; return false; }
					}
					return true;
				}
			}

			bool Init()
			{
				g_dev = VulkanGraphicsDeviceImpl::GetInstance();
				if (!g_dev) return false;

				ImGuiIO& io = ImGui::GetIO();
				io.BackendRendererName = "imgui_impl_aq_vulkan";
				io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;

				g_vs = g_dev->CreateShader("Assets/Shader/ImGuiVK.fx", "VSMain", IShader::ShaderType::VS);
				g_ps = g_dev->CreateShader("Assets/Shader/ImGuiVK.fx", "PSMain", IShader::ShaderType::PS);
				if (!g_vs || !g_ps) return false;

				SamplerDesc sd; sd.filter = FilterMode::MinMagMipLinear;
				sd.addressU = sd.addressV = sd.addressW = AddressMode::Wrap;
				g_sampler = g_dev->CreateSamplerState(sd);

				float initProj[4] = { 0, 0, 0, 0 };
				g_projCB = g_dev->CreateConstantBuffer(initProj, sizeof(initProj));

				if (!g_sampler || !g_projCB)   return false;
				if (!CreateFontTexture())      { EngineAssertMsg(false, "ImGui Vulkan フォント生成失敗"); return false; }
				if (!CreatePipeline())         { EngineAssertMsg(false, "ImGui Vulkan パイプライン生成失敗"); return false; }
				return true;
			}

			void Shutdown()
			{
				if (g_dev && g_pipeline) { vkDestroyPipeline(g_dev->GetDevice(), g_pipeline, nullptr); g_pipeline = VK_NULL_HANDLE; }
				g_vb.reset(); g_ib.reset(); g_projCB.reset(); g_sampler.reset(); g_font.reset();
				g_vs.reset(); g_ps.reset();
				g_vbCap = g_ibCap = 0; g_fontView = VK_NULL_HANDLE; g_dev = nullptr;
			}

			void NewFrame() {}

			void Render(VkCommandBuffer cmd, ImDrawData* drawData)
			{
				if (!g_dev || !g_pipeline || !drawData || drawData->TotalVtxCount <= 0) return;
				if (!EnsureBuffers((uint32_t)drawData->TotalVtxCount, (uint32_t)drawData->TotalIdxCount)) return;

				// 全 draw list の頂点/インデックスを連結してアップロード。
				std::vector<ImDrawVert> verts; verts.reserve(drawData->TotalVtxCount);
				std::vector<ImDrawIdx>  idxs;  idxs.reserve(drawData->TotalIdxCount);
				for (int n = 0; n < drawData->CmdListsCount; ++n)
				{
					const ImDrawList* dl = drawData->CmdLists[n];
					verts.insert(verts.end(), dl->VtxBuffer.Data, dl->VtxBuffer.Data + dl->VtxBuffer.Size);
					idxs.insert(idxs.end(),  dl->IdxBuffer.Data, dl->IdxBuffer.Data + dl->IdxBuffer.Size);
				}
				static_cast<VulkanVertexBuffer*>(g_vb.get())->Update(verts.data(), (uint32_t)(verts.size() * sizeof(ImDrawVert)));
				static_cast<VulkanIndexBuffer*>(g_ib.get())->Update(idxs.data(),   (uint32_t)(idxs.size()  * sizeof(ImDrawIdx)));

				// scale/translate (imgui座標 → Vulkan NDC, Y下のまま)。
				const float L = drawData->DisplayPos.x, T = drawData->DisplayPos.y;
				const float W = drawData->DisplaySize.x, H = drawData->DisplaySize.y;
				if (W <= 0 || H <= 0) return;
				float proj[4] = { 2.0f / W, 2.0f / H, -1.0f - L * (2.0f / W), -1.0f - T * (2.0f / H) };
				static_cast<VulkanConstantBuffer*>(g_projCB.get())->Update(proj);

				vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_pipeline);
				VkViewport vpRect{ 0, 0, W, H, 0.0f, 1.0f };  // 正・非Y-flip (swapchain は確定画像)
				vkCmdSetViewport(cmd, 0, 1, &vpRect);

				auto* vb = static_cast<VulkanVertexBuffer*>(g_vb.get());
				auto* ib = static_cast<VulkanIndexBuffer*>(g_ib.get());
				VkBuffer vbuf = vb->GetBuffer(); VkDeviceSize voff = vb->GetCurrentOffset();
				vkCmdBindVertexBuffers(cmd, 0, 1, &vbuf, &voff);
				vkCmdBindIndexBuffer(cmd, ib->GetBuffer(), ib->GetCurrentOffset(), ib->GetIndexType());

				VkDevice device = g_dev->GetDevice();
				VkPipelineLayout layout = g_dev->GetPipelineLayout()->GetPipelineLayout();
				VkDescriptorSetLayout setLayout = g_dev->GetPipelineLayout()->GetSetLayout();
				VkSampler sampler = static_cast<VulkanSampler*>(g_sampler.get())->GetSampler();
				auto* projCB = static_cast<VulkanConstantBuffer*>(g_projCB.get());

				uint32_t globalVtx = 0, globalIdx = 0;
				for (int n = 0; n < drawData->CmdListsCount; ++n)
				{
					const ImDrawList* dl = drawData->CmdLists[n];
					for (int c = 0; c < dl->CmdBuffer.Size; ++c)
					{
						const ImDrawCmd& dc = dl->CmdBuffer[c];
						if (dc.UserCallback) { continue; }
						// クリップ矩形 → scissor
						ImVec2 cmin(dc.ClipRect.x - L, dc.ClipRect.y - T);
						ImVec2 cmax(dc.ClipRect.z - L, dc.ClipRect.w - T);
						if (cmin.x < 0) cmin.x = 0; if (cmin.y < 0) cmin.y = 0;
						if (cmax.x > W) cmax.x = W; if (cmax.y > H) cmax.y = H;
						if (cmax.x <= cmin.x || cmax.y <= cmin.y) continue;
						VkRect2D sc{ { (int32_t)cmin.x, (int32_t)cmin.y },
						             { (uint32_t)(cmax.x - cmin.x), (uint32_t)(cmax.y - cmin.y) } };
						vkCmdSetScissor(cmd, 0, 1, &sc);

						VkImageView texView = (VkImageView)(uintptr_t)dc.GetTexID();
						if (!texView) texView = g_fontView;

						// descriptor set (engine layout: b0=proj, t0=tex, s0=sampler)
						VkDescriptorSetAllocateInfo ai{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
						ai.descriptorPool = g_dev->GetCurrentDescriptorPool();
						ai.descriptorSetCount = 1; ai.pSetLayouts = &setLayout;
						VkDescriptorSet set = VK_NULL_HANDLE;
						if (vkAllocateDescriptorSets(device, &ai, &set) != VK_SUCCESS) continue;

						VkDescriptorBufferInfo bi{ projCB->GetBuffer(), projCB->GetCurrentOffset(), projCB->GetRange() };
						VkDescriptorImageInfo  ti{}; ti.imageView = texView; ti.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
						VkDescriptorImageInfo  si{}; si.sampler = sampler;
						VkWriteDescriptorSet w[3]{};
						w[0] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET }; w[0].dstSet = set; w[0].dstBinding = VulkanPipelineLayout::CbvBinding(0); w[0].descriptorCount = 1; w[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; w[0].pBufferInfo = &bi;
						w[1] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET }; w[1].dstSet = set; w[1].dstBinding = VulkanPipelineLayout::SrvBinding(0); w[1].descriptorCount = 1; w[1].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE; w[1].pImageInfo = &ti;
						w[2] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET }; w[2].dstSet = set; w[2].dstBinding = VulkanPipelineLayout::SamplerBinding(0); w[2].descriptorCount = 1; w[2].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER; w[2].pImageInfo = &si;
						vkUpdateDescriptorSets(device, 3, w, 0, nullptr);
						vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &set, 0, nullptr);

						vkCmdDrawIndexed(cmd, dc.ElemCount, 1, globalIdx + dc.IdxOffset, globalVtx + dc.VtxOffset, 0);
					}
					globalVtx += dl->VtxBuffer.Size;
					globalIdx += dl->IdxBuffer.Size;
				}
			}
		}
	}
}
#endif // ENGINE_GRAPHICS_VULKAN && AQ_IMGUI
