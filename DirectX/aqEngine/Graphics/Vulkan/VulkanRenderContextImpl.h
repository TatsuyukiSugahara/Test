#pragma once
#include "Graphics/Vulkan/VulkanCommon.h"
#include "Graphics/IRenderContextImpl.h"

namespace aq
{
	namespace graphics
	{
		class VulkanGraphicsDeviceImpl;
		class VulkanShader;
		class VulkanVertexBuffer;
		class VulkanIndexBuffer;
		class VulkanConstantBuffer;
		class VulkanRenderTarget;
		class VulkanDepthMap;
		class VulkanSRV;
		class VulkanSampler;
		class VulkanUAV;

		/**
		 * Vulkan RenderContext Implementor — Phase 1b
		 *
		 * DX11 のイミディエイト設定を「保留ステート＋ Draw 時 flush」へ変換する (D3D12 と同型)。
		 * dynamic rendering を OMSetRenderTargets/Clear/Draw/Present の流れで begin/end する。
		 * Phase 1b はメイン (スワップチェーンプロキシ) 単一カラーターゲット + UBO バインドまで。
		 * テクスチャ(SRV/サンプラ)=Phase2, 深度/MRT/オフスクリーン=Phase3, compute/影=Phase4。
		 * 設計: Graphics/Vulkan/VulkanBackend設計.md §3
		 */
		class VulkanRenderContextImpl : public IRenderContextImpl
		{
		public:
			explicit VulkanRenderContextImpl(VulkanGraphicsDeviceImpl* device) : device_(device) {}

			// device->Present() から呼ばれ、開いている dynamic rendering を閉じる。
			void EndRenderingIfActive();

			// ── レンダーターゲット / クリア ──
			void OMSetRenderTargets(uint32_t numViews, IRenderTarget* renderTarget) override;
			void OMSetMRTRenderTargets(uint32_t numViews, IRenderTarget* const* renderTargets) override;
			void OMSetRenderTargetWithDepth(IRenderTarget& colorRT, IRenderTarget& depthSourceRT) override;
			void OMSetDepthMode(DepthMode mode) override { depth_ = mode; }
			void OMSetBlendMode(BlendMode mode) override { blend_ = mode; }
			void RSSetViewport(float topLeftX, float topLeftY, float width, float height) override;
			void RSSetScissorEnabled(bool enabled) override { scissorEnabled_ = enabled; }
			void RSSetScissorRect(int x, int y, int w, int h) override;
			void ClearRenderTargetView(uint32_t index, float* clearColor) override;
			void ClearDepthBuffer() override { pendingDepthClear_ = true; }

			// ── 入力アセンブラ ──
			void IASetVertexBuffer(IVertexBuffer& vertexBuffer) override;
			void IASetIndexBuffer(IIndexBuffer& indexBuffer) override;
			void IASetPrimitiveTopology(PrimitiveTopology topology) override;
			void IASetInputLayout(IShader& vsShader) override;

			// ── シェーダ / 定数バッファ ──
			void VSSetShader(IShader& shader) override;
			void VSSetConstantBuffer(uint32_t startSlot, IConstantBuffer& constantBuffer) override;
			void PSSetShader(IShader& shader) override;
			void PSSetConstantBuffer(uint32_t startSlot, IConstantBuffer& constantBuffer) override;
			void PSSetShaderResource(uint32_t startSlot, IShaderResourceView& srv) override;
			void PSUnsetShaderResource(uint32_t slot) override;
			void PSSetSampler(uint32_t startSlot, ISamplerState& sampler) override;
			void PSUnsetShader() override { ps_ = nullptr; }

			// ── compute (Phase 4) ──
			void CSSetShader(IShader& shader) override;
			void CSUnsetShader() override { cs_ = nullptr; }
			void CSSetConstantBuffer(uint32_t startSlot, IConstantBuffer& cb) override;
			void CSSetSampler(uint32_t startSlot, ISamplerState& s) override;
			void CSSetShaderResource(uint32_t startSlot, IShaderResourceView& srv) override;
			void CSUnsetShaderResource(uint32_t slot) override;
			void CSSetUnorderedAccessView(uint32_t startSlot, IUnorderedAccessView& uav) override;
			void CSUnsetUnorderedAccessView(uint32_t slot) override;

			// ── 描画 ──
			void Draw(uint32_t vertexCount, uint32_t startVertexLocation) override;
			void DrawIndexed(uint32_t indexCount) override;
			void DrawIndexed(uint32_t indexCount, uint32_t startIndexLocation) override;
			void Dispatch(uint32_t x, uint32_t y, uint32_t z) override;

			void UpdateConstantBuffer(IConstantBuffer& buf, const void* data) override;

			// ── シャドウ深度パス (Phase 4) ──
			void OMSetDepthOnlyTarget(IDepthMap& depthMap) override;
			void OMSetDepthOnlyTargetSlice(IDepthMap& depthMap, uint32_t slice) override;
			void ClearDepthMap(IDepthMap& depthMap) override;
			void ClearDepthMapSlice(IDepthMap& depthMap, uint32_t slice) override;

		private:
			static constexpr uint32_t MAX_CBV     = 6;   // b0..b5
			static constexpr uint32_t MAX_SRV     = 12;  // t0..t11
			static constexpr uint32_t MAX_SAMPLER = 2;   // s0..s1
			static constexpr uint32_t MAX_MRT     = 8;   // SV_Target0..7
			static constexpr uint32_t MAX_UAV     = 8;   // u0..u7

			void BeginRenderingIfNeeded();
			bool FlushGraphics();  // PSO/IA/viewport/descriptor/バリア を確定。false=描画スキップ
			void DrawIndexedInternal(uint32_t indexCount, uint32_t startIndex);
			void BarrierBeforePass();  // rendering scope 外で RT/depth/SRV のレイアウト遷移
			bool FlushCompute();       // compute PSO/descriptor/バリア を確定。false=スキップ

			VulkanGraphicsDeviceImpl* device_ = nullptr;

			// 保留ステート
			VulkanShader*        vs_ = nullptr;
			VulkanShader*        ps_ = nullptr;
			VulkanVertexBuffer*  vb_ = nullptr;
			VulkanIndexBuffer*   ib_ = nullptr;
			VulkanConstantBuffer* cbs_[MAX_CBV] = {};
			VulkanSRV*            srvs_[MAX_SRV] = {};
			VulkanSampler*        samplers_[MAX_SAMPLER] = {};

			// compute 保留ステート
			VulkanShader*         cs_ = nullptr;
			VulkanConstantBuffer* csCbs_[MAX_CBV] = {};
			VulkanSRV*            csSrvs_[MAX_SRV] = {};
			VulkanSampler*        csSamplers_[MAX_SAMPLER] = {};
			VulkanUAV*            csUavs_[MAX_UAV] = {};
			VkPrimitiveTopology  topology_ = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
			DepthMode            depth_ = DepthMode::ReadWrite;
			BlendMode            blend_ = BlendMode::Opaque;

			VulkanRenderTarget*  curRTs_[MAX_MRT] = {};
			uint32_t             rtCount_  = 0;
			VulkanRenderTarget*  depthSrc_ = nullptr;  // 深度 attachment 提供元 (hasDepth の RT)
			VulkanDepthMap*      depthOnlyMap_ = nullptr;  // シャドウ深度のみパス (色なし)
			uint32_t             depthOnlySlice_ = 0;
			VkViewport           viewport_ = {};
			VkRect2D             scissor_  = {};
			bool                 scissorEnabled_ = false;
			bool                 renderingActive_ = false;
			bool                 pendingDepthClear_ = false;
			uint32_t             pendingClearMask_ = 0;
			float                clearColors_[MAX_MRT][4] = {
				{ 0, 0, 0, 1 },
				{ 0, 0, 0, 1 },
				{ 0, 0, 0, 1 },
				{ 0, 0, 0, 1 },
				{ 0, 0, 0, 1 },
				{ 0, 0, 0, 1 },
				{ 0, 0, 0, 1 },
				{ 0, 0, 0, 1 },
			};
			float                clearDepth_    = 1.0f;
		};
	}
}
