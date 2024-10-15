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
		/** �W���u */
		std::queue<JobPackagedTask> jobQueue_;
		std::mutex jobQueueMutex_;

		/** ���[�J�[�X���b�h */
		std::vector<std::thread> workers_;
		std::condition_variable condition_;

		/** �W���u������������ */
		std::atomic<int32_t> jobCounter_;
		std::atomic<bool> completedFlag_;

		/** ��~�t���O */
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

		/** �W���u��ǉ� */
		JobHandle Schedule(const JobFunction& job)
		{
			JobPackagedTask task(job);
			auto future = task.get_future().share();
			{
				std::lock_guard<std::mutex> lock(jobQueueMutex_);
				jobCounter_.fetch_add(1);		// �W���u�ǉ�
				completedFlag_.store(false);	// ������

				// �W���u�ǉ�
				jobQueue_.push(std::move(task));
			}
			// �ҋ@���̃��[�J�[�X���b�h����N����
			condition_.notify_one();
			return future;
		}
		/** �W���u��ǉ�(�ˑ��֌W�ݒ�) */
		JobHandle Schedule(const JobFunction& job, const std::vector<JobHandle>& dependencies)
		{
			// �w��W���u�Ɉˑ��W���u���I���܂őҋ@���鏈����ǉ�
			auto wrapper = [job, dependencies]()
				{
					// �S�Ă̈ˑ��W���u���I���܂őҋ@
					for (auto& depend : dependencies) {
						if (depend.valid()) {
							depend.wait();
						}
					}

					job();
				};

			return Schedule(wrapper);
		}


		/** �S�W���u����������܂őҋ@ */
		void WaitForAll() const
		{
			completedFlag_.wait(false);
		}


	private:
		void WorkerThreadFunction()
		{
			// �������[�v�őҋ@�Ǝ��s���J��Ԃ�
			while (true) {
				JobPackagedTask job;
				{
					// �����ɂ��ҋ@�Ȃ̂�unique_lock��
					std::unique_lock<std::mutex> lock(jobQueueMutex_);
					condition_.wait(lock, [this] 
						{
							// �W���u���ǉ������܂őҋ@
							// ��~�t���O���������ꍇ���N����
							return !jobQueue_.empty() || stopFlag_;
						}
					);

					// ��~�t���O�������Ă��ăW���u�L���[����Ȃ甲����
					if (stopFlag_ && jobQueue_.empty()) {
						break;
					}

					// �W���u�擾
					job = std::move(jobQueue_.front());
					jobQueue_.pop();
				}
				
				// �W���u���s
				job();

				// �W���u�����������̂Ō��炷
				if (jobCounter_.fetch_sub(1) == 1) {
					// �J�E���g��0�ɂȂ����犮���t���O�𗧂ĂĒʒm����
					completedFlag_.store(true);
					completedFlag_.notify_all();
				}
			}
		}


		/**
		 * �V���O���g���p����
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