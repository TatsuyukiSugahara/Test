#pragma once
#include <array>
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include "FrameContext.h"
#include "RenderTargetHandle.h"
#include "Graphics/Lighting.h"
#include "Shadow/ShadowData.h"
#include "RenderConfig.h"

namespace aq
{
	namespace graphics { class RenderContext; }

	namespace rendering
	{
		class RenderCommandList;

		/**
		 * RenderCommandList を専用スレッドで実行するリングバッファ。
		 *
		 * 同期モードは RenderConfig.h の AQ_RENDER_PIPELINED で切り替える。
		 *
		 * 直列 (default):
		 *   Submit → WaitForCompletion() で毎フレーム全完了を待つ。リング 2 スロット。
		 *
		 * 非同期 (AQ_RENDER_PIPELINED):
		 *   Submit → WaitForPipelinedFrame() で「前フレームの全 Submit 完了」だけを待ち、
		 *   今フレーム分は実行中のまま次フレームの構築へ進む（1 フレーム重複）。
		 *     フレーム N   [ゲーム: ECS + BuildRenderFrame + BuildCmdList + Submit]
		 *     フレーム N-1                          [レンダー: Execute + CopyToBackBuffer + Present]
		 *
		 * 重要: 1 フレームに複数回 Submit する構成（オフスクリーン + メイン等）でも正しく
		 * 「フレーム単位」で待てるよう、スロット index ではなく submittedCount_/completedCount_
		 * の累計カウンタで同期する。リング容量は (1フレームの最大Submit数 × 2) 以上が望ましい。
		 * 容量が足りなくても Submit が自然にバックプレッシャーするだけで正しさは保たれる。
		 *
		 * DX12 移行
		 * ----------
		 * Run() の Execute() 呼び出しを ID3D12CommandQueue::ExecuteCommandLists() に置き換え、
		 * 完了通知をフェンスベースに変更する。リング・カウンタの仕組みはそのまま使える。
		 */
		class RenderThread
		{
		public:
			void Initialize(graphics::RenderContext* context);
			void Finalize();

			/**
			 * コマンドリストとフレームのライティング・シャドウデータを渡す。
			 * 両スロットがまだ処理中の場合はブロックする。
			 *
			 * displayRT はこのリスト実行後にバックバッファへコピーする RT を示す。
			 * オフスクリーンやコンピュートのみのフレームでは RenderTargetHandle{} (INVALID) を渡す。
			 */
			void Submit(std::unique_ptr<RenderCommandList> list,
			            RenderTargetHandle                 displayRT,
			            const graphics::LightingData&      lighting,
			            const ShadowCBData&                shadow = {});

			/** 提出済みの全コマンドが完了するまで待機する（直列モード用）。 */
			void WaitForCompletion();

			/**
			 * 前フレームまでに提出された全コマンドの完了を待機する（非同期モード用）。
			 *
			 * フレーム N の Submit 群の直後に呼ぶ。フレーム N-1 の完了だけを待ち、
			 * フレーム N は実行中のまま呼び出し元（ゲームスレッド）はフレーム N+1 の
			 * 構築へ進める。これにより 1 フレーム分の CPU/GPU 重複が得られる。
			 *
			 * 「フレーム」の区切りは本メソッドの呼び出し（= FlushRender）で定義される。
			 * 1 フレーム内の Submit 回数が可変でも累計カウンタで正しく前フレームを待てる。
			 * 前提: フレーム N と N+1 で別のメイン RT を使うこと（Engine 側で毎フレームトグル）。
			 */
			void WaitForPipelinedFrame();

		private:
			void Run();

			// ----- リングバッファ ---------------------------------------------------
			// 非同期モードでは「フレーム N 実行中にフレーム N+1 を構築」するため、
			// (1フレームの最大Submit数=オフスクリーン+メイン=2) × 2フレーム = 4 スロット必要。
			// 直列モードは 2 スロットで足りる（毎フレーム完了を待つため）。
#ifdef AQ_RENDER_PIPELINED
			static constexpr int FRAMES_IN_FLIGHT = 4;
#else
			static constexpr int FRAMES_IN_FLIGHT = 2;
#endif

			struct FrameSlot
			{
				std::unique_ptr<RenderCommandList>         list;
				ConstantBufferPool                         perDrawPool;
				ConstantBufferPool                         materialPool;
				ConstantBufferPool                         bonesPool;
				ConstantBufferPool                         oceanPool;   // b5: OceanCBData
				std::unique_ptr<graphics::IConstantBuffer> lightingCB;
				std::unique_ptr<graphics::IConstantBuffer> shadowCB;
				graphics::LightingData                     lightingData;
				ShadowCBData                               shadowData;
				RenderTargetHandle                         displayRT;
				bool                                       ready = false;
				bool                                       done  = true;

				FrameSlot(uint32_t perDrawSize, uint32_t materialSize, uint32_t bonesSize, uint32_t oceanSize)
					: perDrawPool(perDrawSize)
					, materialPool(materialSize)
					, bonesPool(bonesSize)
					, oceanPool(oceanSize)
				{}
			};

			std::unique_ptr<FrameSlot> slots_[FRAMES_IN_FLIGHT];
			int writeSlot_ = 0;
			int readSlot_  = 0;

			// ----- フレーム単位の同期カウンタ（すべて mutex_ 保護）-----------------
			// submittedCount_ : Submit された総コマンド数
			// completedCount_ : レンダースレッドが実行完了した総コマンド数
			// prevFrameMark_  : 直前の WaitForPipelinedFrame() 時点の submittedCount_
			//                   （= 前フレーム末尾の提出点。FlushRender=ゲームスレッド専用）
			uint64_t submittedCount_ = 0;
			uint64_t completedCount_ = 0;
			uint64_t prevFrameMark_  = 0;

			// ----- スレッド制御 ----------------------------------------------------
			graphics::RenderContext* context_ = nullptr;
			std::thread              thread_;
			std::mutex               mutex_;
			std::condition_variable  cvGame_;    // ゲームスレッドを起こす（スロット解放時）
			std::condition_variable  cvRender_;  // レンダースレッドを起こす（スロット充填時）
			std::atomic<bool>        running_ { false };
		};
	}
}
