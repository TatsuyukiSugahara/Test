#include "aq.h"
#include "FrameContext.h"
#ifdef AQ_DEBUG_IMGUI
#include <cstdlib>
#include <mutex>
#endif


namespace aq
{
	namespace rendering
	{
#ifdef AQ_DEBUG_IMGUI
		namespace
		{
			constexpr uint32_t CATEGORY_COUNT = static_cast<uint32_t>(DrawStatCategory::Count);

			struct PrevKey { const void* k[4] = {}; bool valid = false; };

			DrawStats s_current;                    // レンダースレッドだけが触る
			PrevKey   s_prev[CATEGORY_COUNT];       // 同上。カテゴリごとに直前のドローのキー
			DrawStats s_last;                       // 公開用(s_lastMutex で保護)
			std::mutex s_lastMutex;
			uint32_t  s_frameCounter = 0;
		}


		void DrawStatsCollector::BeginFrame()
		{
			s_current = DrawStats{};
			for (auto& prev : s_prev) { prev = PrevKey{}; }
		}


		void DrawStatsCollector::OnDraw(const DrawStatCategory category, const void* k0, const void* k1,
		                                const void* k2, const void* k3, const uint32_t stateCalls)
		{
			const uint32_t index = static_cast<uint32_t>(category);
			DrawStats::Category& c = s_current.category[index];
			PrevKey& prev = s_prev[index];

			++c.draws;
			c.stateCalls += stateCalls;
			// 最初のドローは「遷移」に数える(その状態を 1 回は設定する必要があるため)。
			if (!prev.valid || prev.k[0] != k0 || prev.k[1] != k1 || prev.k[2] != k2 || prev.k[3] != k3) {
				++c.transitions;
			}
			prev.k[0] = k0; prev.k[1] = k1; prev.k[2] = k2; prev.k[3] = k3;
			prev.valid = true;
		}


		void DrawStatsCollector::EndFrame()
		{
			{
				std::lock_guard<std::mutex> lock(s_lastMutex);
				s_last = s_current;
			}

			// 環境変数 AQ_DRAW_STATS_LOG があれば 120 フレームごとにログへ出す(P3 の実測・比較用)。
			static const bool s_logEnabled = (std::getenv("AQ_DRAW_STATS_LOG") != nullptr);
			if (s_logEnabled && (++s_frameCounter % 120u) == 0u)
			{
				const DrawStats& d = s_current;
				aq::StartupMarkf("[drawstats] GBuffer %u/%u  Shadow %u/%u  Forward %u/%u  Instanced %u/%u  (draws/transitions)  stateCalls G=%u F=%u",
				                 d.category[0].draws, d.category[0].transitions,
				                 d.category[1].draws, d.category[1].transitions,
				                 d.category[2].draws, d.category[2].transitions,
				                 d.category[3].draws, d.category[3].transitions,
				                 d.category[0].stateCalls, d.category[2].stateCalls);
			}
		}


		DrawStats DrawStatsCollector::Last()
		{
			std::lock_guard<std::mutex> lock(s_lastMutex);
			return s_last;
		}
#endif // AQ_DEBUG_IMGUI


		ConstantBufferPool::ConstantBufferPool(uint32_t cbSize)
			: cbSize_(cbSize)
		{
		}


		graphics::IConstantBuffer* ConstantBufferPool::Allocate()
		{
			if (cursor_ == static_cast<uint32_t>(pool_.size()))
			{
				auto cb = graphics::GraphicsDevice::Get().CreateConstantBuffer(nullptr, cbSize_);
				if (!cb) return nullptr;  // device lost / OOM
				pool_.push_back(std::move(cb));
			}
			return pool_[cursor_++].get();
		}


		void ConstantBufferPool::Reset()
		{
			cursor_ = 0;
		}
	}
}
