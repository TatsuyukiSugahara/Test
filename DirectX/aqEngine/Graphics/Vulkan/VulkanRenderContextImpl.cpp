#include "aq.h"
#ifdef ENGINE_GRAPHICS_VULKAN
#include "Graphics/Vulkan/VulkanRenderContextImpl.h"
#include "Graphics/Vulkan/VulkanGraphicsDeviceImpl.h"
#include "Graphics/Vulkan/VulkanShader.h"
#include "Graphics/Vulkan/VulkanBuffers.h"
#include "Graphics/Vulkan/VulkanPipelineLayout.h"
#include "Graphics/Vulkan/VulkanPipelineCache.h"
#include "Graphics/Vulkan/VulkanRenderTarget.h"
#include "Graphics/Vulkan/VulkanDepthMap.h"
#include "Graphics/Vulkan/VulkanResources.h"
#include <array>

namespace aq
{
	namespace graphics
	{
		// ── レンダーターゲット / クリア ───────────────────────────
		void VulkanRenderContextImpl::OMSetRenderTargets(uint32_t /*numViews*/, IRenderTarget* renderTarget)
		{
			EndRenderingIfActive();
			depthOnlyMap_ = nullptr;
			rtCount_ = renderTarget ? 1 : 0;
			curRTs_[0] = static_cast<VulkanRenderTarget*>(renderTarget);
			// 自前深度を持つ RT ならそれを深度ソースにする (postprocess 等は深度なし)。
			depthSrc_ = (curRTs_[0] && curRTs_[0]->HasDepth()) ? curRTs_[0] : nullptr;
		}

		void VulkanRenderContextImpl::OMSetMRTRenderTargets(uint32_t numViews, IRenderTarget* const* renderTargets)
		{
			EndRenderingIfActive();
			depthOnlyMap_ = nullptr;
			rtCount_ = (numViews < MAX_MRT) ? numViews : MAX_MRT;
			depthSrc_ = nullptr;
			for (uint32_t i = 0; i < rtCount_; ++i)
			{
				curRTs_[i] = static_cast<VulkanRenderTarget*>(renderTargets[i]);
				if (!depthSrc_ && curRTs_[i] && curRTs_[i]->HasDepth()) depthSrc_ = curRTs_[i];
			}
		}

		void VulkanRenderContextImpl::OMSetRenderTargetWithDepth(IRenderTarget& colorRT, IRenderTarget& depthSourceRT)
		{
			EndRenderingIfActive();
			depthOnlyMap_ = nullptr;
			rtCount_   = 1;
			curRTs_[0] = static_cast<VulkanRenderTarget*>(&colorRT);
			depthSrc_  = static_cast<VulkanRenderTarget*>(&depthSourceRT);
		}

		// ── シャドウ深度のみパス ──
		void VulkanRenderContextImpl::OMSetDepthOnlyTargetSlice(IDepthMap& depthMap, uint32_t slice)
		{
			EndRenderingIfActive();
			depthOnlyMap_   = static_cast<VulkanDepthMap*>(&depthMap);
			depthOnlySlice_ = slice;
			rtCount_  = 0;
			depthSrc_ = nullptr;
		}
		void VulkanRenderContextImpl::OMSetDepthOnlyTarget(IDepthMap& depthMap) { OMSetDepthOnlyTargetSlice(depthMap, 0); }
		void VulkanRenderContextImpl::ClearDepthMapSlice(IDepthMap& /*depthMap*/, uint32_t /*slice*/) { pendingDepthClear_ = true; }
		void VulkanRenderContextImpl::ClearDepthMap(IDepthMap& depthMap) { ClearDepthMapSlice(depthMap, 0); }

		void VulkanRenderContextImpl::RSSetViewport(float topLeftX, float topLeftY, float width, float height)
		{
			// Y-flip: 負の height で Vulkan の Y下向き NDC を D3D の Y上向きに合わせる (HLSL/行列は不変)。
			viewport_.x        = topLeftX;
			viewport_.y        = topLeftY + height;
			viewport_.width    = width;
			viewport_.height   = -height;
			viewport_.minDepth = 0.0f;
			viewport_.maxDepth = 1.0f;
		}

		void VulkanRenderContextImpl::RSSetScissorRect(int x, int y, int w, int h)
		{
			scissor_.offset = { x, y };
			scissor_.extent = { (uint32_t)(w > 0 ? w : 0), (uint32_t)(h > 0 ? h : 0) };
		}

		void VulkanRenderContextImpl::ClearRenderTargetView(uint32_t index, float* clearColor)
		{
			// クリアは即 begin せず保留し、Draw 時の BeginRendering で loadOp=CLEAR に集約する
			// (SRV バリアを rendering scope 外で打てるようにするため)。
			if (index >= rtCount_ || index >= MAX_MRT)
				return;

			if (clearColor) { for (int i = 0; i < 4; ++i) clearColors_[index][i] = clearColor[i]; }
			pendingClearMask_ |= (1u << index);
		}

		// ── 入力アセンブラ ───────────────────────────────────────
		void VulkanRenderContextImpl::IASetVertexBuffer(IVertexBuffer& vertexBuffer)
		{
			vb_ = static_cast<VulkanVertexBuffer*>(&vertexBuffer);
		}
		void VulkanRenderContextImpl::IASetIndexBuffer(IIndexBuffer& indexBuffer)
		{
			ib_ = static_cast<VulkanIndexBuffer*>(&indexBuffer);
		}
		void VulkanRenderContextImpl::IASetPrimitiveTopology(PrimitiveTopology topology)
		{
			topology_ = ToVkTopology(topology);
		}
		void VulkanRenderContextImpl::IASetInputLayout(IShader& vsShader)
		{
			// 入力レイアウトは VS のリフレクション由来。VS シェーダを記録する (VSSetShader と同源)。
			vs_ = static_cast<VulkanShader*>(&vsShader);
		}

		// ── シェーダ / 定数バッファ ───────────────────────────────
		void VulkanRenderContextImpl::VSSetShader(IShader& shader) { vs_ = static_cast<VulkanShader*>(&shader); }
		void VulkanRenderContextImpl::PSSetShader(IShader& shader) { ps_ = static_cast<VulkanShader*>(&shader); }

		void VulkanRenderContextImpl::VSSetConstantBuffer(uint32_t startSlot, IConstantBuffer& constantBuffer)
		{
			if (startSlot < MAX_CBV) cbs_[startSlot] = static_cast<VulkanConstantBuffer*>(&constantBuffer);
		}
		void VulkanRenderContextImpl::PSSetConstantBuffer(uint32_t startSlot, IConstantBuffer& constantBuffer)
		{
			// 単一 DescriptorSet のため VS/PS とも同じ binding b# を共有する。
			if (startSlot < MAX_CBV) cbs_[startSlot] = static_cast<VulkanConstantBuffer*>(&constantBuffer);
		}

		void VulkanRenderContextImpl::UpdateConstantBuffer(IConstantBuffer& buf, const void* data)
		{
			static_cast<VulkanConstantBuffer&>(buf).Update(data);
		}

		// ── テクスチャ / サンプラ (Phase 2) ──────────────────────
		void VulkanRenderContextImpl::PSSetShaderResource(uint32_t startSlot, IShaderResourceView& srv)
		{
			if (startSlot >= MAX_SRV) return;
			// DeferredSRV ラッパ等を GetNativeHandle() で実体 VulkanSRV* へ解決 (D3D12 と同方式)。
			srvs_[startSlot] = static_cast<VulkanSRV*>(srv.GetNativeHandle());
		}
		void VulkanRenderContextImpl::PSUnsetShaderResource(uint32_t slot)
		{
			if (slot < MAX_SRV) srvs_[slot] = nullptr;
		}
		void VulkanRenderContextImpl::PSSetSampler(uint32_t startSlot, ISamplerState& sampler)
		{
			if (startSlot < MAX_SAMPLER) samplers_[startSlot] = static_cast<VulkanSampler*>(&sampler);
		}

		namespace
		{
			// 1 画像のレイアウト遷移を記録する (rendering scope の外で呼ぶこと)。
			void TransitionImg(VkCommandBuffer cmd, VkImage image, VkImageAspectFlags aspect,
			                   VkImageLayout oldL, VkImageLayout newL)
			{
				VkImageMemoryBarrier2 b{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
				b.srcStageMask  = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
				b.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT;
				b.dstStageMask  = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
				b.dstAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT;
				b.oldLayout = oldL; b.newLayout = newL; b.image = image;
				b.subresourceRange = { aspect, 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS };
				VkDependencyInfo dep{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
				dep.imageMemoryBarrierCount = 1; dep.pImageMemoryBarriers = &b;
				vkCmdPipelineBarrier2(cmd, &dep);
			}
		}

		// ── パス開始前のレイアウト遷移 (rendering scope 外) ──
		void VulkanRenderContextImpl::BarrierBeforePass()
		{
			VkCommandBuffer cmd = device_->GetCommandBuffer();

			// 束縛 SRV (オフスクリーン RT / 深度) を SHADER_READ_ONLY へ。通常テクスチャは LayoutPtr=null。
			for (uint32_t i = 0; i < MAX_SRV; ++i)
			{
				if (!srvs_[i]) continue;
				VkImageLayout* lp = srvs_[i]->LayoutPtr();
				if (!lp || *lp == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) continue;
				TransitionImg(cmd, srvs_[i]->GetImage(), srvs_[i]->Aspect(), *lp, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
				*lp = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			}
			// カラー RT を COLOR_ATTACHMENT へ (proxy は device が BeginFrame で遷移済みのため除外)。
			for (uint32_t i = 0; i < rtCount_; ++i)
			{
				VulkanRenderTarget* rt = curRTs_[i];
				if (!rt || rt->IsProxy()) continue;
				VkImageLayout* lp = rt->ColorLayoutPtr();
				if (*lp == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) continue;
				TransitionImg(cmd, rt->GetImage(), VK_IMAGE_ASPECT_COLOR_BIT, *lp, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
				*lp = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			}
			// 深度を DEPTH_ATTACHMENT へ。
			if (depthSrc_ && depthSrc_->HasDepth())
			{
				VkImageLayout* lp = depthSrc_->DepthLayoutPtr();
				if (*lp != VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL)
				{
					TransitionImg(cmd, depthSrc_->GetDepthImage(), VK_IMAGE_ASPECT_DEPTH_BIT, *lp, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);
					*lp = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
				}
			}
			// シャドウ深度のみパス: 深度マップ(配列)を DEPTH_ATTACHMENT へ。
			if (depthOnlyMap_)
			{
				VkImageLayout* lp = depthOnlyMap_->LayoutPtr();
				if (*lp != VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL)
				{
					TransitionImg(cmd, depthOnlyMap_->GetImage(), VK_IMAGE_ASPECT_DEPTH_BIT, *lp, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);
					*lp = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
				}
			}
		}

		// ── dynamic rendering begin/end ───────────────────────────
		void VulkanRenderContextImpl::BeginRenderingIfNeeded()
		{
			if (renderingActive_) return;

			// シャドウ深度のみパス (色なし)。
			if (depthOnlyMap_)
			{
				VkCommandBuffer cmd = device_->GetCommandBuffer();
				const uint32_t res = depthOnlyMap_->GetResolution();
				VkRenderingAttachmentInfo depth{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
				depth.imageView   = depthOnlyMap_->GetSliceView(depthOnlySlice_);
				depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
				depth.loadOp      = pendingDepthClear_ ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
				depth.storeOp     = VK_ATTACHMENT_STORE_OP_STORE;
				depth.clearValue.depthStencil = { clearDepth_, 0 };
				VkRenderingInfo ri{ VK_STRUCTURE_TYPE_RENDERING_INFO };
				ri.renderArea       = { { 0, 0 }, { res, res } };
				ri.layerCount       = 1;
				ri.pDepthAttachment = &depth;
				vkCmdBeginRendering(cmd, &ri);
				renderingActive_   = true;
				pendingDepthClear_ = false;
				return;
			}

			if (rtCount_ == 0 || !curRTs_[0]) return;
			VkCommandBuffer cmd = device_->GetCommandBuffer();

			VkRenderingAttachmentInfo colors[MAX_MRT]{};
			for (uint32_t i = 0; i < rtCount_; ++i)
			{
				const bool clear = (pendingClearMask_ & (1u << i)) != 0;
				colors[i] = { VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
				colors[i].imageView   = curRTs_[i]->GetView();
				colors[i].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
				colors[i].loadOp      = clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
				colors[i].storeOp     = VK_ATTACHMENT_STORE_OP_STORE;
				colors[i].clearValue.color = { { clearColors_[i][0], clearColors_[i][1], clearColors_[i][2], clearColors_[i][3] } };
			}

			VkRenderingAttachmentInfo depth{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
			const bool hasDepth = depthSrc_ && depthSrc_->HasDepth();
			if (hasDepth)
			{
				depth.imageView   = depthSrc_->GetDepthView();
				depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
				depth.loadOp      = pendingDepthClear_ ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
				depth.storeOp     = VK_ATTACHMENT_STORE_OP_STORE;
				depth.clearValue.depthStencil = { clearDepth_, 0 };
			}

			const VkExtent2D extent = curRTs_[0]->GetExtent();
			VkRenderingInfo ri{ VK_STRUCTURE_TYPE_RENDERING_INFO };
			ri.renderArea           = { { 0, 0 }, extent };
			ri.layerCount           = 1;
			ri.colorAttachmentCount = rtCount_;
			ri.pColorAttachments    = colors;
			ri.pDepthAttachment     = hasDepth ? &depth : nullptr;

			vkCmdBeginRendering(cmd, &ri);
			renderingActive_  = true;
			pendingClearMask_ &= ~((1u << rtCount_) - 1u);
			pendingDepthClear_= false;
		}

		void VulkanRenderContextImpl::EndRenderingIfActive()
		{
			// 描画が無くてもクリアが保留されていれば begin/end で適用する。
			// depth-only (シャドウ) パスは rtCount_ == 0 のため、color/depth-only の両ターゲットを見る。
			const bool hasColorTarget     = (rtCount_ > 0 && curRTs_[0]);
			const bool hasDepthOnlyTarget = (depthOnlyMap_ != nullptr);
			if (!renderingActive_ && (pendingClearMask_ || pendingDepthClear_) &&
			    (hasColorTarget || hasDepthOnlyTarget))
			{
				device_->BeginFrameIfNeeded();
				BarrierBeforePass();
				BeginRenderingIfNeeded();
			}
			if (!renderingActive_) return;
			vkCmdEndRendering(device_->GetCommandBuffer());
			renderingActive_ = false;
		}

		// ── flush + draw ─────────────────────────────────────────
		bool VulkanRenderContextImpl::FlushGraphics()
		{
			const bool depthOnly = (depthOnlyMap_ != nullptr);
			if (!vs_ || (!depthOnly && (!ps_ || rtCount_ == 0 || !curRTs_[0]))) return false;
			device_->BeginFrameIfNeeded();
			// バリアは rendering scope の外で打つ (パス先頭=まだ rendering 未開始のときのみ)。
			if (!renderingActive_) BarrierBeforePass();
			BeginRenderingIfNeeded();
			VkCommandBuffer cmd = device_->GetCommandBuffer();

			// PSO 解決 (depthOnly は PS 無し・color 無し・深度書込)
			VulkanPipelineKey key{};
			key.vs       = vs_->GetModule();
			key.ps       = ps_ ? ps_->GetModule() : VK_NULL_HANDLE;
			key.topology = (uint32_t)topology_;
			key.blend    = (uint8_t)blend_;
			key.depth    = (uint8_t)(depthOnly ? DepthMode::ReadWrite : depth_);
			key.rtCount  = depthOnly ? 0 : (uint8_t)rtCount_;
			for (uint32_t i = 0; i < key.rtCount; ++i) key.rtFormats[i] = curRTs_[i]->GetFormat();
			key.dsFormat = depthOnly ? VK_FORMAT_D32_SFLOAT
			             : ((depthSrc_ && depthSrc_->HasDepth()) ? depthSrc_->GetDepthFormat() : VK_FORMAT_UNDEFINED);
			key.vertexStride = vb_ ? vb_->GetStride() : 0;  // 実 VB stride (部分宣言 VS の誤読防止)

			VkPipeline pipeline = device_->GetPipelineCache()->GetOrCreate(
				device_->GetDevice(), device_->GetPipelineLayout()->GetPipelineLayout(), key, vs_);
			if (!pipeline) return false;
			vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

			// 動的ステート (viewport/scissor)
			const VkExtent2D ext = depthOnly ? VkExtent2D{ depthOnlyMap_->GetResolution(), depthOnlyMap_->GetResolution() }
			                                 : curRTs_[0]->GetExtent();
			vkCmdSetViewport(cmd, 0, 1, &viewport_);
			VkRect2D sc = scissorEnabled_ ? scissor_ : VkRect2D{ { 0, 0 }, ext };
			vkCmdSetScissor(cmd, 0, 1, &sc);

			// 頂点バッファ
			if (vb_)
			{
				VkBuffer vbuf = vb_->GetBuffer();
				VkDeviceSize off = vb_->GetCurrentOffset();
				vkCmdBindVertexBuffers(cmd, 0, 1, &vbuf, &off);
			}

			// ディスクリプタセット (UBO のみ。テクスチャ/サンプラは Phase 2)
			VkDescriptorSetLayout setLayout = device_->GetPipelineLayout()->GetSetLayout();
			VkDescriptorSetAllocateInfo ai{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
			ai.descriptorPool     = device_->GetCurrentDescriptorPool();
			ai.descriptorSetCount = 1;
			ai.pSetLayouts        = &setLayout;
			VkDescriptorSet set = VK_NULL_HANDLE;
			if (vkAllocateDescriptorSets(device_->GetDevice(), &ai, &set) == VK_SUCCESS)
			{
				constexpr uint32_t MAX_W = MAX_CBV + MAX_SRV + MAX_SAMPLER;
				std::array<VkDescriptorBufferInfo, MAX_CBV>            bufInfos{};
				std::array<VkDescriptorImageInfo, MAX_SRV + MAX_SAMPLER> imgInfos{};
				std::array<VkWriteDescriptorSet, MAX_W>               writes{};
				uint32_t n = 0, bi = 0, ii = 0;

				for (uint32_t i = 0; i < MAX_CBV; ++i)
				{
					if (!cbs_[i]) continue;
					bufInfos[bi] = { cbs_[i]->GetBuffer(), cbs_[i]->GetCurrentOffset(), cbs_[i]->GetRange() };
					VkWriteDescriptorSet& w = writes[n] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
					w.dstSet = set; w.dstBinding = VulkanPipelineLayout::CbvBinding(i);
					w.descriptorCount = 1; w.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
					w.pBufferInfo = &bufInfos[bi];
					++bi; ++n;
				}
				// SRV: シェーダが静的参照する未バインドスロットを埋めるため全スロットへ書く
				// (未設定はデフォルト 1x1 白)。バリアは BarrierBeforePass が束縛分のみ処理。
				const VkImageView defView = device_->GetDefaultTextureView();
				for (uint32_t i = 0; i < MAX_SRV; ++i)
				{
					VkImageView view = (srvs_[i] && srvs_[i]->GetImageView()) ? srvs_[i]->GetImageView() : defView;
					if (view == VK_NULL_HANDLE) continue;
					imgInfos[ii] = {};
					imgInfos[ii].imageView   = view;
					imgInfos[ii].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
					VkWriteDescriptorSet& w = writes[n] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
					w.dstSet = set; w.dstBinding = VulkanPipelineLayout::SrvBinding(i);
					w.descriptorCount = 1; w.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
					w.pImageInfo = &imgInfos[ii];
					++ii; ++n;
				}
				const VkSampler defSamp = device_->GetDefaultSampler();
				for (uint32_t i = 0; i < MAX_SAMPLER; ++i)
				{
					VkSampler s = (samplers_[i] && samplers_[i]->GetSampler()) ? samplers_[i]->GetSampler() : defSamp;
					if (s == VK_NULL_HANDLE) continue;
					imgInfos[ii] = {};
					imgInfos[ii].sampler = s;
					VkWriteDescriptorSet& w = writes[n] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
					w.dstSet = set; w.dstBinding = VulkanPipelineLayout::SamplerBinding(i);
					w.descriptorCount = 1; w.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
					w.pImageInfo = &imgInfos[ii];
					++ii; ++n;
				}
				if (n > 0) vkUpdateDescriptorSets(device_->GetDevice(), n, writes.data(), 0, nullptr);
				vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
				                        device_->GetPipelineLayout()->GetPipelineLayout(), 0, 1, &set, 0, nullptr);
			}
			return true;
		}

		void VulkanRenderContextImpl::Draw(uint32_t vertexCount, uint32_t startVertexLocation)
		{
			if (!FlushGraphics()) return;
			vkCmdDraw(device_->GetCommandBuffer(), vertexCount, 1, startVertexLocation, 0);
		}

		void VulkanRenderContextImpl::DrawIndexedInternal(uint32_t indexCount, uint32_t startIndex)
		{
			if (!ib_ || !FlushGraphics()) return;
			VkCommandBuffer cmd = device_->GetCommandBuffer();
			vkCmdBindIndexBuffer(cmd, ib_->GetBuffer(), ib_->GetCurrentOffset(), ib_->GetIndexType());
			vkCmdDrawIndexed(cmd, indexCount, 1, startIndex, 0, 0);
		}

		void VulkanRenderContextImpl::DrawIndexed(uint32_t indexCount)                          { DrawIndexedInternal(indexCount, 0); }
		void VulkanRenderContextImpl::DrawIndexed(uint32_t indexCount, uint32_t startIndexLoc)  { DrawIndexedInternal(indexCount, startIndexLoc); }

		// ── compute (Phase 4) ────────────────────────────────────
		void VulkanRenderContextImpl::CSSetShader(IShader& shader) { cs_ = static_cast<VulkanShader*>(&shader); }
		void VulkanRenderContextImpl::CSSetConstantBuffer(uint32_t s, IConstantBuffer& cb)
		{ if (s < MAX_CBV) csCbs_[s] = static_cast<VulkanConstantBuffer*>(&cb); }
		void VulkanRenderContextImpl::CSSetSampler(uint32_t s, ISamplerState& smp)
		{ if (s < MAX_SAMPLER) csSamplers_[s] = static_cast<VulkanSampler*>(&smp); }
		void VulkanRenderContextImpl::CSSetShaderResource(uint32_t s, IShaderResourceView& srv)
		{ if (s < MAX_SRV) csSrvs_[s] = static_cast<VulkanSRV*>(srv.GetNativeHandle()); }
		void VulkanRenderContextImpl::CSUnsetShaderResource(uint32_t s) { if (s < MAX_SRV) csSrvs_[s] = nullptr; }
		void VulkanRenderContextImpl::CSSetUnorderedAccessView(uint32_t s, IUnorderedAccessView& uav)
		{ if (s < MAX_UAV) csUavs_[s] = static_cast<VulkanUAV*>(&uav); }
		void VulkanRenderContextImpl::CSUnsetUnorderedAccessView(uint32_t s) { if (s < MAX_UAV) csUavs_[s] = nullptr; }

		bool VulkanRenderContextImpl::FlushCompute()
		{
			if (!cs_ || !cs_->GetModule()) return false;
			device_->BeginFrameIfNeeded();
			EndRenderingIfActive();  // compute は rendering scope の外
			VkCommandBuffer cmd = device_->GetCommandBuffer();

			// バリア: 束縛 SRV → SHADER_READ_ONLY、束縛 UAV → GENERAL。
			for (uint32_t i = 0; i < MAX_SRV; ++i)
			{
				if (!csSrvs_[i]) continue;
				VkImageLayout* lp = csSrvs_[i]->LayoutPtr();
				if (!lp || *lp == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) continue;
				TransitionImg(cmd, csSrvs_[i]->GetImage(), csSrvs_[i]->Aspect(), *lp, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
				*lp = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			}
			for (uint32_t i = 0; i < MAX_UAV; ++i)
			{
				if (!csUavs_[i]) continue;
				VkImageLayout* lp = csUavs_[i]->LayoutPtr();
				if (!lp || *lp == VK_IMAGE_LAYOUT_GENERAL) continue;
				TransitionImg(cmd, csUavs_[i]->GetImage(), VK_IMAGE_ASPECT_COLOR_BIT, *lp, VK_IMAGE_LAYOUT_GENERAL);
				*lp = VK_IMAGE_LAYOUT_GENERAL;
			}

			VkPipeline pipeline = device_->GetPipelineCache()->GetOrCreateCompute(
				device_->GetDevice(), device_->GetPipelineLayout()->GetComputePipelineLayout(), cs_->GetModule());
			if (!pipeline) return false;
			vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);

			VkDescriptorSetLayout setLayout = device_->GetPipelineLayout()->GetComputeSetLayout();
			VkDescriptorSetAllocateInfo ai{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
			ai.descriptorPool     = device_->GetCurrentDescriptorPool();
			ai.descriptorSetCount = 1;
			ai.pSetLayouts        = &setLayout;
			VkDescriptorSet set = VK_NULL_HANDLE;
			if (vkAllocateDescriptorSets(device_->GetDevice(), &ai, &set) != VK_SUCCESS) return false;

			constexpr uint32_t MAX_W = MAX_CBV + MAX_SRV + MAX_SAMPLER + MAX_UAV;
			std::array<VkDescriptorBufferInfo, MAX_CBV>                      bufInfos{};
			std::array<VkDescriptorImageInfo, MAX_SRV + MAX_SAMPLER + MAX_UAV> imgInfos{};
			std::array<VkWriteDescriptorSet, MAX_W>                          writes{};
			uint32_t n = 0, bi = 0, ii = 0;
			const VkImageView defView = device_->GetDefaultTextureView();
			const VkSampler   defSamp = device_->GetDefaultSampler();

			for (uint32_t i = 0; i < MAX_CBV; ++i)
			{
				if (!csCbs_[i]) continue;
				bufInfos[bi] = { csCbs_[i]->GetBuffer(), csCbs_[i]->GetCurrentOffset(), csCbs_[i]->GetRange() };
				VkWriteDescriptorSet& w = writes[n] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
				w.dstSet = set; w.dstBinding = VulkanPipelineLayout::CbvBinding(i);
				w.descriptorCount = 1; w.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
				w.pBufferInfo = &bufInfos[bi]; ++bi; ++n;
			}
			for (uint32_t i = 0; i < MAX_SRV; ++i)
			{
				VkImageView view = (csSrvs_[i] && csSrvs_[i]->GetImageView()) ? csSrvs_[i]->GetImageView() : defView;
				if (!view) continue;
				imgInfos[ii] = {}; imgInfos[ii].imageView = view; imgInfos[ii].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				VkWriteDescriptorSet& w = writes[n] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
				w.dstSet = set; w.dstBinding = VulkanPipelineLayout::SrvBinding(i);
				w.descriptorCount = 1; w.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
				w.pImageInfo = &imgInfos[ii]; ++ii; ++n;
			}
			for (uint32_t i = 0; i < MAX_SAMPLER; ++i)
			{
				VkSampler s = (csSamplers_[i] && csSamplers_[i]->GetSampler()) ? csSamplers_[i]->GetSampler() : defSamp;
				if (!s) continue;
				imgInfos[ii] = {}; imgInfos[ii].sampler = s;
				VkWriteDescriptorSet& w = writes[n] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
				w.dstSet = set; w.dstBinding = VulkanPipelineLayout::SamplerBinding(i);
				w.descriptorCount = 1; w.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
				w.pImageInfo = &imgInfos[ii]; ++ii; ++n;
			}
			for (uint32_t i = 0; i < MAX_UAV; ++i)
			{
				if (!csUavs_[i] || !csUavs_[i]->GetStorageView()) continue;
				imgInfos[ii] = {}; imgInfos[ii].imageView = csUavs_[i]->GetStorageView(); imgInfos[ii].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
				VkWriteDescriptorSet& w = writes[n] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
				w.dstSet = set; w.dstBinding = VulkanPipelineLayout::UavBinding(i);
				w.descriptorCount = 1; w.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
				w.pImageInfo = &imgInfos[ii]; ++ii; ++n;
			}
			if (n > 0) vkUpdateDescriptorSets(device_->GetDevice(), n, writes.data(), 0, nullptr);
			vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
			                        device_->GetPipelineLayout()->GetComputePipelineLayout(), 0, 1, &set, 0, nullptr);
			return true;
		}

		void VulkanRenderContextImpl::Dispatch(uint32_t x, uint32_t y, uint32_t z)
		{
			if (!FlushCompute()) return;
			vkCmdDispatch(device_->GetCommandBuffer(), x, y, z);
			// compute 書込を後続の読み取りへ可視化 (次パスのレイアウト遷移バリアでも担保されるが明示)。
			VkMemoryBarrier2 mb{ VK_STRUCTURE_TYPE_MEMORY_BARRIER_2 };
			mb.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT; mb.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
			mb.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;   mb.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT;
			VkDependencyInfo dep{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
			dep.memoryBarrierCount = 1; dep.pMemoryBarriers = &mb;
			vkCmdPipelineBarrier2(device_->GetCommandBuffer(), &dep);
		}
	}
}
#endif // ENGINE_GRAPHICS_VULKAN
