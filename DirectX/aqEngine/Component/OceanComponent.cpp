#include "aq.h"
#include "OceanComponent.h"

namespace aq
{
	namespace ecs
	{
		void OceanComponent::Initialize(const ocean::OceanParams& params)
		{
#ifdef AQ_DEBUG_IMGUI
			const char* nmp1 = params.normalMapPath1;
			const char* nmp2 = params.normalMapPath2;
#endif
			params_ = params;
			mesh_.Initialize(params_);
			state_ = State::Completed;
#ifdef AQ_DEBUG_IMGUI
			if (nmp1 != normalMapPath1_.c_str()) normalMapPath1_ = nmp1 ? nmp1 : "";
			if (nmp2 != normalMapPath2_.c_str()) normalMapPath2_ = nmp2 ? nmp2 : "";
			params_.normalMapPath1 = nullptr;
			params_.normalMapPath2 = nullptr;
#endif
		}
	}
}
