#pragma once
#include <cstdint>
#include <memory>
#include <vector>
#include "Graphics/IBuffer.h"
#include "Rendering/RenderTargetHandle.h"

namespace aq
{
	namespace graphics { class GraphicsDevice; }

	namespace rendering
	{
		/**
		 * Provides per-draw constant buffer slots for one frame.
		 *
		 * DX11: wraps a growing pool of DEFAULT constant buffers updated via UpdateSubresource.
		 * DX12: replace with an upload-heap slice allocator that returns GPU virtual addresses.
		 */
		class IConstantBufferPool
		{
		public:
			virtual ~IConstantBufferPool() = default;
			virtual graphics::IConstantBuffer* Allocate() = 0;
			virtual void Reset() = 0;
		};


		/**
		 * Default IConstantBufferPool implementation.
		 * Allocates IConstantBuffer objects via GraphicsDevice on demand; reuses them
		 * across frames after Reset().  Grows monotonically to the per-frame high-water mark.
		 */
		class ConstantBufferPool final : public IConstantBufferPool
		{
		public:
			explicit ConstantBufferPool(uint32_t cbSize);

			graphics::IConstantBuffer* Allocate() override;
			void Reset() override;

		private:
			uint32_t cbSize_;
			uint32_t cursor_ = 0;
			std::vector<std::unique_ptr<graphics::IConstantBuffer>> pool_;
		};


#ifdef AQ_DEBUG_IMGUI
		/**
		 * ドロー数と状態遷移数の計測(設計書/エンジン課題解消設計.md P3-A)。AQ_DEBUG_IMGUI のときだけ存在する。
		 *
		 * 「状態設定の呼び出し回数」は、並べ替えても各ドローが同じ設定を発行するので減らない。
		 * ソートの効果は **状態遷移数**(直前のドローと `(vs, ps, テクスチャ 0, 頂点バッファ)` が異なった回数)で測る。
		 *
		 * スレッド: 書くのはレンダースレッドだけ(BeginFrame / OnDraw / EndFrame)。
		 * EndFrame が直前フレームの値を排他付きで公開し、メインスレッド(パネル)は Last() で読む。
		 */
		enum class DrawStatCategory : uint32_t { GBuffer = 0, Shadow, Forward, Instanced, Count };

		struct DrawStats
		{
			struct Category
			{
				uint32_t draws       = 0;   // ドロー数
				uint32_t transitions = 0;   // 状態遷移数(直前のドローとキーが違った回数)
				uint32_t stateCalls  = 0;   // 状態設定呼び出しの概算(GBuffer / Forward のみ。他は 0)
			};
			Category category[static_cast<uint32_t>(DrawStatCategory::Count)];
		};

		class DrawStatsCollector
		{
		public:
			static void BeginFrame();
			/** キーは 4 つのポインタ(vs / ps / テクスチャ 0 / 頂点バッファ。無いものは nullptr) */
			static void OnDraw(DrawStatCategory category, const void* k0, const void* k1,
			                   const void* k2, const void* k3, uint32_t stateCalls);
			static void EndFrame();
			static DrawStats Last();
		};

#define AQ_DRAW_STAT(category, k0, k1, k2, k3, stateCalls) \
	::aq::rendering::DrawStatsCollector::OnDraw(::aq::rendering::DrawStatCategory::category, (k0), (k1), (k2), (k3), (stateCalls))
#else
#define AQ_DRAW_STAT(category, k0, k1, k2, k3, stateCalls) ((void)0)
#endif


		/**
		 * Per-frame render resources passed to every IRenderCommand::Execute() call.
		 *
		 * perDrawCBPool  : b0 用 (world/view/proj)、draw ごとに Allocate()
		 * materialCBPool : b2 用 (MaterialCBData)、draw ごとに Allocate()
		 * lightingCB     : b1 用、フレーム先頭で 1 回更新し全 draw で共有 (raw ptr, 寿命は FrameSlot)
		 * bonesCBPool    : b4 用 (128 × Matrix4x4 = 8192 bytes)、skeletal draw ごとに Allocate()
		 * oceanCBPool    : b5 用 (OceanCBData = 192 bytes)、ocean draw ごとに Allocate()
		 */
		struct FrameContext
		{
			IConstantBufferPool*       perDrawCBPool  = nullptr;
			IConstantBufferPool*       materialCBPool = nullptr;
			graphics::IConstantBuffer* lightingCB     = nullptr;
			graphics::IConstantBuffer* shadowCB       = nullptr; // b3: per-frame shadow data
			IConstantBufferPool*       bonesCBPool    = nullptr; // b4: bone matrices (skeletal mesh)
			IConstantBufferPool*       oceanCBPool    = nullptr; // b5: OceanCBData (海描画用)
			RenderTargetHandle         displayRT;               // ポストプロセス後の確定 RT (UI 描画先)
		};
	}
}
