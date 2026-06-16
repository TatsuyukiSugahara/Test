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

namespace engine
{
	namespace graphics { class RenderContext; }

	namespace rendering
	{
		class RenderCommandList;

		/**
		 * RenderCommandList を専用スレッドで実行する 2 フレームのリングバッファ。
		 *
		 * パイプライン重複
		 * ----------------
		 *   フレーム N   [ゲーム: ECS + BuildRenderFrame + BuildCmdList + Submit]
		 *   フレーム N-1                              [レンダー: Execute + CopyToBackBuffer + Present]
		 *
		 * Submit() は両スロットが使用中のとき（ゲームがフレーム N+1 を Submit しようとしたが
		 * レンダースレッドがまだフレーム N-1 を処理中）のみブロックする。
		 * 現状の同期使用（Submit + WaitForCompletion）ではリングは透過的に動作する。
		 *
		 * DX12 移行
		 * ----------
		 * Run() の Execute() 呼び出しを ID3D12CommandQueue::ExecuteCommandLists() に置き換え、
		 * WaitForCompletion() をフェンスベースの待機に変更する。リングの仕組みはそのまま使える。
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

			/** 直近の Submit() が完了するまで待機する。 */
			void WaitForCompletion();

			/**
			 * 1フレーム前の Submit() が完了するまで待機する。
			 *
			 * ダブルバッファ RT が整ったら IApplication::FlushRender() でこれを使う。
			 * ゲームスレッドはフレーム N を構築しながら、レンダースレッドはフレーム N-1 を実行する。
			 * FlushRender はフレーム N ではなくフレーム N-1 の完了を待てばよいため、
			 * 1フレーム分の CPU/GPU 重複が得られる。
			 *
			 * 単一共有 RT の場合はダブルバッファ RT が揃うまで WaitForCompletion() を使うこと。
			 */
			void WaitForPreviousFrame();

		private:
			void Run();

			// ----- リングバッファ ---------------------------------------------------
			static constexpr int FRAMES_IN_FLIGHT = 2;

			struct FrameSlot
			{
				std::unique_ptr<RenderCommandList>         list;
				ConstantBufferPool                         perDrawPool;
				ConstantBufferPool                         materialPool;
				std::unique_ptr<graphics::IConstantBuffer> lightingCB;
				std::unique_ptr<graphics::IConstantBuffer> shadowCB;
				graphics::LightingData                     lightingData;
				ShadowCBData                               shadowData;
				RenderTargetHandle                         displayRT;
				bool                                       ready = false;
				bool                                       done  = true;

				FrameSlot(uint32_t perDrawSize, uint32_t materialSize)
					: perDrawPool(perDrawSize)
					, materialPool(materialSize)
				{}
			};

			std::unique_ptr<FrameSlot> slots_[FRAMES_IN_FLIGHT];
			int writeSlot_ = 0;
			int readSlot_  = 0;

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
