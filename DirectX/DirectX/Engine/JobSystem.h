#pragma once
#include <cstdint>
#include <thread>
#include <vector>
#include <condition_variable>
#include <queue>
#include <mutex>
#include <functional>
#include <future>


namespace engine
{
	using JobHandle = std::shared_future<void>;


	class JobSystem
	{
	private:
		using JobFunction = std::function<void()>;
		using JobPackagedTask = std::packaged_task<void()>;

	private:
		/** ジョブ */
		std::queue<JobPackagedTask> jobQueue_;
		std::mutex jobQueueMutex_;

		/** ワーカースレッド */
		std::vector<std::thread> workers_;
		std::condition_variable condition_;

		/** ジョブが完了したか */
		std::atomic<int32_t> jobCounter_;
		std::atomic<bool> completedFlag_;

		/** 停止フラグ */
		std::atomic<bool> stopFlag_;

	private:
		JobSystem();
		~JobSystem();


	public:
		void CreateWorkerThread(const uint32_t threadCount)
		{
			for (uint32_t i = 0; i < threadCount; ++i) {
				workers_.emplace_back([this] { this->WorkerThreadFunction(); });
			}
		}

		/** ジョブを追加 */
		JobHandle Schedule(const JobFunction& job)
		{
			JobPackagedTask task(job);
			auto future = task.get_future().share();
			{
				std::lock_guard<std::mutex> lock(jobQueueMutex_);
				jobCounter_.fetch_add(1);		// ジョブ追加
				completedFlag_.store(false);	// 未完了

				// ジョブ追加
				jobQueue_.push(std::move(task));
			}
			// 待機中のワーカースレッドを一つ起こす
			condition_.notify_one();
			return future;
		}
		/** ジョブを追加(依存関係設定) */
		JobHandle Schedule(const JobFunction& job, const std::vector<JobHandle>& dependencies)
		{
			// 指定ジョブに依存ジョブが終わるまで待機する処理を追加
			auto wrapper = [job, dependencies]()
				{
					// 全ての依存ジョブが終わるまで待機
					for (auto& depend : dependencies) {
						if (depend.valid()) {
							depend.wait();
						}
					}

					job();
				};

			return Schedule(wrapper);
		}


		/** 全ジョブが完了するまで待機 */
		void WaitForAll() const
		{
			completedFlag_.wait(false);
		}


	private:
		void WorkerThreadFunction()
		{
			// 無限ループで待機と実行を繰り返す
			while (true) {
				JobPackagedTask job;
				{
					// 条件による待機なのでunique_lockに
					std::unique_lock<std::mutex> lock(jobQueueMutex_);
					condition_.wait(lock, [this] 
						{
							// ジョブが追加されるまで待機
							// 停止フラグが立った場合も起きる
							return !jobQueue_.empty() || stopFlag_;
						}
					);

					// 停止フラグが立っていてジョブキューが空なら抜ける
					if (stopFlag_ && jobQueue_.empty()) {
						break;
					}

					// ジョブ取得
					job = std::move(jobQueue_.front());
					jobQueue_.pop();
				}
				
				// ジョブ実行
				job();

				// ジョブを処理したので減らす
				if (jobCounter_.fetch_sub(1) == 1) {
					// カウントが0になったら完了フラグを立てて通知する
					completedFlag_.store(true);
					completedFlag_.notify_all();
				}
			}
		}


		/**
		 * シングルトン用処理
		 */
	private:
		static JobSystem* instance_;


	public:
		static void Initialize()
		{
			if (instance_ == nullptr) {
				instance_ = new JobSystem();
			}
		}
		static JobSystem& Get()
		{
			return *instance_;
		}
		static void Finalize()
		{
			if (instance_) {
				delete instance_;
				instance_ = nullptr;
			}
		}
	};
}