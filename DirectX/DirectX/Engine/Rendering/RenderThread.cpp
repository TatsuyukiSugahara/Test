#include "RenderThread.h"
#include "RenderCommandList.h"
#include "Graphics/RenderContext.h"
#include "Graphics/GraphicsTypes.h"
#include "Graphics/GraphicsDevice.h"
#include "Graphics/Lighting.h"


namespace engine
{
	namespace rendering
	{
		void RenderThread::Initialize(graphics::RenderContext* context)
		{
			context_ = context;

			graphics::LightingData defaultLighting{};
			ShadowCBData           defaultShadow{};
			for (auto& slot : slots_)
			{
				slot = std::make_unique<FrameSlot>(
					sizeof(graphics::VSConstantBuffer),
					sizeof(graphics::MaterialCBData));

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
			}
			cvRender_.notify_one();
		}


		void RenderThread::WaitForCompletion()
		{
			// writeSlot_ を読む前にロックを取得する。
			// Submit() が writeSlot_ を更新するのと同じ mutex を使うことで競合を防ぐ。
			std::unique_lock<std::mutex> lock(mutex_);
			int prev = (writeSlot_ - 1 + FRAMES_IN_FLIGHT) % FRAMES_IN_FLIGHT;
			cvGame_.wait(lock, [this, prev] { return slots_[prev]->done; });
		}


		void RenderThread::WaitForPreviousFrame()
		{
			// FRAMES_IN_FLIGHT==2 のとき、Submit(list_N) 後の writeSlot_ は
			// list_{N-1}（前フレーム）が入っていたスロットと一致する。
			// そこを待つことで、レンダースレッドがフレーム N-1 の描画中に
			// ゲームスレッドがフレーム N を構築できる（1フレーム分の重複）。
			// 前提: リングスロットごとに独立したレンダーターゲット（ダブルバッファRT）が必要。
			std::unique_lock<std::mutex> lock(mutex_);
			int prevPrev = writeSlot_;  // FRAMES_IN_FLIGHT==2 では (writeSlot_ - 2 + 2) % 2 と等価
			cvGame_.wait(lock, [this, prevPrev] { return slots_[prevPrev]->done; });
		}


		void RenderThread::Run()
		{
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
						// Submit/WaitForCompletion の呼び出し元をアンブロックするため
						// 全スロットを done にする。
						// 未実行リストは Finalize() 内で join() 後に slot->list.reset() される。
						for (auto& s : slots_) { s->done = true; s->list.reset(); }
						break;
					}

					slot  = readSlot_;
					list  = std::move(slots_[readSlot_]->list);
					slots_[readSlot_]->ready = false;
				}

				if (list)
				{
					// b1 ライティング CB / b3 シャドウ CB をフレーム先頭で 1 回更新
					context_->UpdateSubresource(*slots_[slot]->lightingCB, slots_[slot]->lightingData);
					if (slots_[slot]->shadowCB) {
						context_->UpdateSubresource(*slots_[slot]->shadowCB, slots_[slot]->shadowData);
					}

					slots_[slot]->perDrawPool.Reset();
					slots_[slot]->materialPool.Reset();
					FrameContext fc {
						&slots_[slot]->perDrawPool,
						&slots_[slot]->materialPool,
						slots_[slot]->lightingCB.get(),
						slots_[slot]->shadowCB.get()
					};
					list->Execute(*context_, fc);
					list->Reset();  // shared_ptr を解放し、アリーナカーソルをリセット

					// CopyToBackBuffer と Present は D3D11 デバイスコンテキストへの呼び出しであり、
					// このスレッド（immediate context の唯一の所有者）で実行しなければならない。
					// メインスレッドは下の done = true が設定されるまで FlushRender() 内で
					// ブロックしているため、コンテキストへの同時アクセスは発生しない。
					const RenderTargetHandle displayRT = slots_[slot]->displayRT;
					const uint32_t rtCount = graphics::GraphicsDevice::Get().GetMainRenderTargetCount();
					if (displayRT.IsValid() && displayRT.index < rtCount)
					{
						auto& rt = graphics::GraphicsDevice::Get().GetMainRenderTarget(displayRT.index);
						graphics::GraphicsDevice::Get().CopyToBackBuffer(rt);
						graphics::GraphicsDevice::Get().Present();
					}
				}

				{
					std::lock_guard<std::mutex> lock(mutex_);
					slots_[slot]->done = true;
					readSlot_ = (readSlot_ + 1) % FRAMES_IN_FLIGHT;
				}
				cvGame_.notify_all();
			}

			cvGame_.notify_all();
		}
	}
}
