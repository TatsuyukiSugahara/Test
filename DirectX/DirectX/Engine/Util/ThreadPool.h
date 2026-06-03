#pragma once
#include <cstdint>
#include <thread>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <future>
#include <atomic>


namespace engine
{
	namespace util
	{
		/**
		 * スレッドプール
		 * Submit() でタスクをキューに積み、std::future で完了を受け取る
		 */
		class ThreadPool
		{
		public:
			explicit ThreadPool(uint32_t threadCount);
			~ThreadPool();

			ThreadPool(const ThreadPool&) = delete;
			ThreadPool& operator=(const ThreadPool&) = delete;

			/**
			 * タスクをキューに追加する
			 * 戻り値: std::future<F の戻り値型>
			 */
			template<typename F, typename... Args>
			auto Submit(F&& f, Args&&... args)
				-> std::future<decltype(std::declval<F>()(std::declval<Args>()...))>
			{
				using ReturnType = decltype(std::declval<F>()(std::declval<Args>()...));

				auto task = std::make_shared<std::packaged_task<ReturnType()>>(
					std::bind(std::forward<F>(f), std::forward<Args>(args)...)
				);
				std::future<ReturnType> future = task->get_future();
				{
					std::lock_guard<std::mutex> lock(mutex_);
					tasks_.emplace([task] { (*task)(); });
				}
				condition_.notify_one();
				return future;
			}

			uint32_t ThreadCount() const { return static_cast<uint32_t>(workers_.size()); }

		private:
			void WorkerThread();

		private:
			std::vector<std::thread>          workers_;
			std::queue<std::function<void()>> tasks_;
			std::mutex                        mutex_;
			std::condition_variable           condition_;
			std::atomic<bool>                 stop_;


			/**
			 * シングルトン
			 */
		private:
			static ThreadPool* instance_;

		public:
			/** threadCount = 0 のとき論理コア数を使用 */
			static void Initialize(uint32_t threadCount = 0)
			{
				if (instance_ == nullptr) {
					const uint32_t hwCount = std::thread::hardware_concurrency();
					const uint32_t count = (threadCount > 0) ? threadCount : (hwCount > 0 ? hwCount : 1u);
					instance_ = new ThreadPool(count);
				}
			}
			static ThreadPool& Get() { return *instance_; }
			static void Finalize()
			{
				if (instance_) {
					delete instance_;
					instance_ = nullptr;
				}
			}
		};
	}
}
