#pragma once
#include "ECS/ECS.h"
#include "Ocean/OceanMesh.h"
#include "Ocean/OceanData.h"

namespace aq
{
	namespace ecs
	{
		// ============================================================
		// OceanComponent — 海サーフェス ECS コンポーネント
		//
		// 使い方:
		//   auto entity = EntityContext::Get().CreateEntity<TransformComponent, OceanComponent>();
		//   auto* tc = entity.GetComponent<TransformComponent>();
		//   tc->transform.localPosition.Set(-50.f, -1.f, -50.f);
		//
		//   auto* ocean = entity.GetComponent<OceanComponent>();
		//   ocean::OceanParams params;
		//   params.size = 200.0f;
		//   ocean->Initialize(params);
		// ============================================================
		class OceanComponent : public IComponent
		{
			ecsComponent(aq::ecs::OceanComponent);

		public:
			void Initialize(const ocean::OceanParams& params);

			bool IsCompleted() const { return state_ == State::Completed; }

			ocean::OceanMesh*        GetMesh()   { return &mesh_; }
			const ocean::OceanParams& GetParams() const { return params_; }
			ocean::OceanParams&       GetParams()       { return params_; }

		private:
			enum class State : uint8_t { Invalid, Completed };
			State              state_  = State::Invalid;
			ocean::OceanParams params_;
			ocean::OceanMesh   mesh_;
		};
	}
}
