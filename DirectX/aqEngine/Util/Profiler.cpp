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


		void Profiler::PublishThisThread()
		{
			ThreadData& td = Local();
			{
				std::lock_guard<std::mutex> lock(td.publishMutex);
				std::swap(td.samples, td.display);
			}
			td.samples.clear();
			td.stack.clear();
		}


		void Profiler::PublishWorkers()
		{
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
			for (auto& tdp : threads_)
			{
				ThreadData* td = tdp.get();
				ThreadFrame tf;
				tf.threadId   = td->threadId;
				tf.orderIndex = td->orderIndex;
				tf.name       = td->name;
				{
					std::lock_guard<std::mutex> lock(td->publishMutex);
					tf.samples = td->display;
				}
				out.push_back(std::move(tf));
			}
		}
	}
}
