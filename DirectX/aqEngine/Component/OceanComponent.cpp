#include "aq.h"
#include "OceanComponent.h"

namespace aq
{
	namespace ecs
	{
		void OceanComponent::Initialize(const ocean::OceanParams& params)
		{
			params_ = params;
			mesh_.Initialize(params_);
			state_ = State::Completed;
		}
	}
}
