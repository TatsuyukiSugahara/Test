#include "aq.h"
#include "RenderThread.h"
#include "Graphics/RenderContext.h"
#include "Graphics/GraphicsTypes.h"
#include "Graphics/Lighting.h"
#include "Ocean/OceanData.h"
#include "Util/Profiler.h"


namespace aq
{
	namespace rendering
	{
		void RenderThread::Initialize(graphics::RenderContext* context)
		{
			context_ = context;

			graphics::LightingData defaultLighting{};
			ShadowCBData           defaultShadow{};
			constexpr uint32_t kBonesCBSize  = 128u * 64u;                   // 128 × sizeof(Matrix4x4)
			constexpr uint32_t kOceanCBSize  = sizeof(ocean::OceanCBData);   // 192 bytes
			for (auto& slot : slots_)
			{
				slot = std::make_unique<FrameSlot>(
					sizeof(graphics::VSConstantBuffer),
					sizeof(graphics::MaterialCBData),
					kBonesCBSize,
					kOceanCBSize);

				slot->lightingCB = graphics::GraphicsDevice::Get().CreateConstantBuffer(
					&defaultLighting, sizeof(defaultLighting));
				slot->shadowCB = graphics::GraphicsDevice::Get().CreateConstantBuffer(
					&defaultShadow, sizeof(defaultShadow));
			}

			running_.store(true);
			thread_ = std::thread(&RenderThread::Run, this);
		}


		void RenderThread::Finalize()
		{
			{
				std::lock_guard<std::mutex> lock(mutex_);
				running_.store(false);
				// 全スロットを ready にしてレンダースレッドを起こし、!running_ を確認させる。
				for (auto& slot : slots_) slot->ready = true;
			}
			cvRender_.notify_one();

			if (thread_.joinable()) thread_.join();

			// 未実行のまま残ったリストを破棄する。
			// ~RenderCommandList() が Reset() を呼ぶため、shared_ptr の参照カウントは
			// join() 後のここで正しく減算される。
			for (auto& slot : slots_) slot->list.reset();
		}


		void RenderThread::Submit(std::unique_ptr<RenderCommandList> list,
		                          RenderTargetHandle                 displayRT,
		                          const graphics::LightingData&      lighting,
		                          const ShadowCBData&                shadow)
		{
			{
				std::unique_lock<std::mutex> lock(mutex_);
				cvGame_.wait(lock, [this] { return slots_[writeSlot_]->done; });

				slots_[writeSlot_]->list         = std::move(list);
				slots_[writeSlot_]->displayRT    = displayRT;
				slots_[writeSlot_]->lightingData = lighting;
				slots_[writeSlot_]->shadowData   = shadow;
				slots_[writeSlot_]->ready        = true;
				slots_[writeSlot_]->done         = false;
				writeSlot_ = (writeSlot_ + 1) % FRAMES_IN_FLIGHT;
				++submittedCount_;
			}
			cvRender_.notify_one();
		}


		void RenderThread::WaitForCompletion()
		{
			// 提出済みの全コマンドが完了するまで待つ（直列モード）。
			std::unique_lock<std::mutex> lock(mutex_);
			cvGame_.wait(lock, [this] { return completedCount_ >= submittedCount_; });
		}


		void RenderThread::WaitForPipelinedFrame()
		{
			// 非同期モード。フレーム N の Submit 群の直後に呼ばれる前提。
			// 入った時点で submittedCount_ = S_N、prevFrameMark_ = S_{N-1}。
			// フレーム N-1 の全コマンド完了（completedCount_ >= S_{N-1}）だけを待ち、
			// フレーム N は実行中のまま戻る → 呼び出し元はフレーム N+1 の構築へ進める。
			std::unique_lock<std::mutex> lock(mutex_);
			cvGame_.wait(lock, [this] { return completedCount_ >= prevFrameMark_; });
			prevFrameMark_ = submittedCount_;  // 今フレーム末尾の提出点を次回用に記録
		}


		void RenderThread::Run()
		{
#ifdef AQ_PROFILE_ENABLED
			profile::Profiler::Get().SetThreadName("Render");
			profile::Profiler::Get().MarkSelfPublishing();
#endif
			while (true)
			{
				int slot;
				std::unique_ptr<RenderCommandList> list;

				{
					std::unique_lock<std::mutex> lock(mutex_);
					cvRender_.wait(lock, [this]
					{
						return slots_[readSlot_]->ready || !running_.load();
					});

					if (!running_.load())
					{
						// Submit/WaitForCompletion/WaitForPipelinedFrame の呼び出し元を
						// アンブロックするため全スロットを done にし、カウンタも追いつかせる。
						// 未実行リストは Finalize() 内で join() 後に slot->list.reset() される。
						for (auto& s : slots_) { s->done = true; s->list.reset(); }
						completedCount_ = submittedCount_;
						break;
					}

					slot  = readSlot_;
					list  = std::move(slots_[readSlot_]->list);
					slots_[readSlot_]->ready = false;
				}

				if (list)
				{
					AQ_PROFILE_SCOPE("RenderThread::Execute");
					// b1 ライティング CB / b3 シャドウ CB をフレーム先頭で 1 回更新
					context_->UpdateSubresource(*slots_[slot]->lightingCB, slots_[slot]->lightingData);
					if (slots_[slot]->shadowCB) {
						context_->UpdateSubresource(*slots_[slot]->shadowCB, slots_[slot]->shadowData);
					}

					slots_[slot]->perDrawPool.Reset();
					slots_[slot]->materialPool.Reset();
					slots_[slot]->bonesPool.Reset();
					slots_[slot]->oceanPool.Reset();
					FrameContext fc {
						&slots_[slot]->perDrawPool,
						&slots_[slot]->materialPool,
						slots_[slot]->lightingCB.get(),
						slots_[slot]->shadowCB.get(),
						&slots_[slot]->bonesPool,
						&slots_[slot]->oceanPool,
						slots_[slot]->displayRT
					};
					{
						AQ_PROFILE_SCOPE("CommandList");
						list->Execute(*context_, fc);
					}
					list->Reset();  // shared_ptr を解放し、アリーナカーソルをリセット

					// CopyToBackBuffer と Present は D3D11 デバイスコンテキストへの呼び出しであり、
					// このスレッド（immediate context の唯一の所有者）で実行しなければならない。
					// メインスレッドは下の done = true が設定されるまで FlushRender() 内で
					// ブロックしているため、コンテキストへの同時アクセスは発生しない。
					const RenderTargetHandle displayRT = slots_[slot]->displayRT;
					if (displayRT.IsValid())
					{
						AQ_PROFILE_SCOPE("Present");
						auto* rt = graphics::GraphicsDevice::Get().GetRenderTarget(displayRT);
						if (rt)
						{
							graphics::GraphicsDevice::Get().CopyToBackBuffer(*rt);
							graphics::GraphicsDevice::Get().Present();
						}
					}
				}

				{
					std::lock_guard<std::mutex> lock(mutex_);
					slots_[slot]->done = true;
					++completedCount_;
					readSlot_ = (readSlot_ + 1) % FRAMES_IN_FLIGHT;
				}
				cvGame_.notify_all();

#ifdef AQ_PROFILE_ENABLED
				// このフレームの計測結果を publish (Execute スコープは上で閉じている)
				profile::Profiler::Get().PublishThisThread();
#endif
			}

			cvGame_.notify_all();
		}
	}
}
