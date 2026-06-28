#include "aq.h"
#include "Profiler.h"
#include <windows.h>


namespace aq
{
	namespace profile
	{
		namespace
		{
			int64_t QPCNow()
			{
				LARGE_INTEGER c;
				QueryPerformanceCounter(&c);
				return c.QuadPart;
			}
		}


		Profiler& Profiler::Get()
		{
			static Profiler instance;
			return instance;
		}


		Profiler::Profiler()
		{
			LARGE_INTEGER freq;
			QueryPerformanceFrequency(&freq);
			ticksToMs_ = 1000.0 / static_cast<double>(freq.QuadPart);
		}


		Profiler::ThreadData& Profiler::Local()
		{
			thread_local ThreadData* tls = nullptr;
			if (tls == nullptr)
			{
				auto td = std::make_shared<ThreadData>();
				td->threadId   = static_cast<uint64_t>(GetCurrentThreadId());
				td->orderIndex = nextOrderIndex_.fetch_add(1);
				td->name       = "Thread " + std::to_string(td->orderIndex);
				{
					std::lock_guard<std::mutex> lock(registryMutex_);
					threads_.push_back(td);
				}
				tls = td.get();
			}
			return *tls;
		}


		void Profiler::SetThreadName(const char* name)
		{
			Local().name = name;
		}


		void Profiler::MarkSelfPublishing()
		{
			Local().selfPublishing = true;
		}


		void Profiler::PushScope(const char* name)
		{
			ThreadData& td = Local();
			Sample s;
			s.name        = name;
			s.parentIndex = td.stack.empty() ? -1 : td.stack.back();
			s.depth       = static_cast<int>(td.stack.size());
			s.startTick   = QPCNow();
			const int index = static_cast<int>(td.samples.size());
			td.samples.push_back(s);
			td.stack.push_back(index);
		}


		void Profiler::PopScope()
		{
			ThreadData& td = Local();
			if (td.stack.empty()) return;
			const int index = td.stack.back();
			td.stack.pop_back();
			const int64_t end = QPCNow();
			td.samples[index].durationMs =
				static_cast<double>(end - td.samples[index].startTick) * ticksToMs_;
		}


		namespace { constexpr size_t kDisplayHistory = 8; }


		void Profiler::PublishThisThread()
		{
			ThreadData& td = Local();
			{
				std::lock_guard<std::mutex> lock(td.publishMutex);
				std::swap(td.samples, td.display);
				if (td.selfPublishing)
				{
					// 直近フレームを履歴に積む (CaptureSnapshot の周期マッチング用)。
					td.history.push_back(td.display);
					if (td.history.size() > kDisplayHistory)
						td.history.erase(td.history.begin());
				}
			}
			td.samples.clear();
			td.stack.clear();
		}


		void Profiler::PublishWorkers()
		{
			// PublishWorkers() はメインスレッドが毎フレーム 1 回だけ呼ぶ。
			// その呼び出し間隔を実フレーム時間として記録する。
			const int64_t now = QPCNow();
			if (lastFramePublishTick_ != 0)
				frameMs_ = static_cast<double>(now - lastFramePublishTick_) * ticksToMs_;
			lastFramePublishTick_ = now;

			ThreadData& self = Local();
			std::lock_guard<std::mutex> reg(registryMutex_);
			for (auto& tdp : threads_)
			{
				ThreadData* td = tdp.get();
				if (td == &self)            continue;  // 自身は PublishThisThread で処理済み
				if (td->selfPublishing)     continue;  // レンダースレッド等は自分で publish
				{
					std::lock_guard<std::mutex> lock(td->publishMutex);
					std::swap(td->samples, td->display);
				}
				td->samples.clear();
				td->stack.clear();
			}
		}


		void Profiler::CaptureSnapshot(std::vector<ThreadFrame>& out)
		{
			out.clear();
			std::lock_guard<std::mutex> reg(registryMutex_);
			out.reserve(threads_.size());

			// --- Pass 1: 基準フレーム周期を求める -----------------------------------
			// 非 selfPublishing スレッド (Main / Worker) の表示サンプルの最古 start を
			// 周期開始とし、直近フレーム時間 (frameMs_) を周期長とする。
			int64_t refStart = INT64_MAX;
			for (auto& tdp : threads_)
			{
				ThreadData* td = tdp.get();
				if (td->selfPublishing) continue;
				std::lock_guard<std::mutex> lock(td->publishMutex);
				for (const Sample& s : td->display)
					if (s.startTick < refStart) refStart = s.startTick;
			}
			const int64_t periodTicks = (ticksToMs_ > 0.0 && frameMs_ > 0.0)
				? static_cast<int64_t>(frameMs_ / ticksToMs_) : (INT64_MAX / 2);
			const int64_t refEnd = (refStart == INT64_MAX) ? INT64_MAX : refStart + periodTicks;

			auto sampleRange = [this](const std::vector<Sample>& v, int64_t& mn, int64_t& mx) {
				mn = INT64_MAX; mx = INT64_MIN;
				for (const Sample& s : v)
				{
					if (s.startTick < mn) mn = s.startTick;
					const int64_t e = s.startTick + static_cast<int64_t>(s.durationMs / ticksToMs_);
					if (e > mx) mx = e;
				}
			};

			// --- Pass 2: 出力構築 ---------------------------------------------------
			for (auto& tdp : threads_)
			{
				ThreadData* td = tdp.get();
				ThreadFrame tf;
				tf.threadId   = td->threadId;
				tf.orderIndex = td->orderIndex;
				tf.name       = td->name;
				{
					std::lock_guard<std::mutex> lock(td->publishMutex);
					if (!td->selfPublishing || td->history.empty() || refStart == INT64_MAX)
					{
						tf.samples = td->display;
					}
					else
					{
						// 基準周期と最も重なる履歴フレーム = Main と同時に走っていた実行。
						// 重なりが無ければ周期中心に最も近いものを次善で選ぶ。
						const std::vector<Sample>* best = &td->display;
						int64_t bestScore = INT64_MIN;
						const int64_t refCenter = refStart + periodTicks / 2;
						for (const std::vector<Sample>& h : td->history)
						{
							if (h.empty()) continue;
							int64_t hMin, hMax;
							sampleRange(h, hMin, hMax);
							const int64_t lo = (hMin > refStart) ? hMin : refStart;
							const int64_t hi = (hMax < refEnd)   ? hMax : refEnd;
							int64_t score = (hi > lo) ? (hi - lo) : 0;  // 重なり tick 数
							if (score == 0)
							{
								const int64_t hC = (hMin + hMax) / 2;
								const int64_t dist = (hC > refCenter) ? (hC - refCenter) : (refCenter - hC);
								score = -dist;  // 重なり無し: 中心が近いほど高スコア (負値)
							}
							if (score > bestScore) { bestScore = score; best = &h; }
						}
						tf.samples = *best;
					}
				}
				out.push_back(std::move(tf));
			}
		}
	}
}
