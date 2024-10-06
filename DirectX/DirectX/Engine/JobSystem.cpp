#include "JobSystem.h"


namespace engine
{


	JobSystem* JobSystem::instance_ = nullptr;


	JobSystem::JobSystem()
		: jobCounter_(0)
		, completedFlag_(false)
		, stopFlag_(false)
	{
	}


	JobSystem::~JobSystem()
	{
		// �S�Ẵ��[�J�[�X���b�h���~������
		stopFlag_.store(true);

		// �S�Ẵ��[�J�[�X���b�h���N����
		condition_.notify_all();

		// �S�Ẵ��[�J�[�X���b�h���I������̂�҂�
		for (auto& worker : workers_) {
			if (worker.joinable()) {
				worker.join();
			}
		}
	}
}