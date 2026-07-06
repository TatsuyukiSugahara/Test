#include "aq.h"
#include "ThreadPool.h"
#include "Profiler.h"


namespace aq
{
	namespace util
	{
		ThreadPool* ThreadPool::instance_ = nullptr;


		ThreadPool::ThreadPool(uint32_t threadCount)
			: stop_(false)
		{
			for (uint32_t i = 0; i < threadCount; ++i) {
				workers_.emplace_back([this, i] { WorkerThread(i); });
			}
		}


		ThreadPool::~ThreadPool()
		{
			{
				std::lock_guard<std::mutex> lock(mutex_);
				stop_.store(true);
			}
			condition_.notify_all();
			for (auto& worker : workers_) {
				if (worker.joinable()) {
					worker.join();
				}
			}
		}


		void ThreadPool::WorkerThread(uint32_t index)
		{
#ifdef AQ_PROFILE_ENABLED
			profile::Profiler::Get().SetThreadName(("Worker " + std::to_string(index)).c_str());
#endif
			while (true) {
				std::function<void()> task;
				{
					std::unique_lock<std::mutex> lock(mutex_);
					condition_.wait(lock, [this] {
						return stop_.load() || !tasks_.empty();
					});
					if (stop_.load() && tasks_.empty()) {
						return;
					}
					task = std::move(tasks_.front());
					tasks_.pop();
				}
				task();
			}
		}
	}
}
