#pragma once
#include "Scene.h"
#include "ECS/Entity.h"

namespace app
{
	namespace battle
	{
		class BattleScene : public app::IScene
		{
		public:
			void Initialize() override;
			void Update() override;
			void Finalize() override;

			aq::math::Vector3 GetFocusPosition() const override;


		public:
			// TODO：Hash値などにする予定
			static constexpr uint32_t GetID() { return 1; }

		private:
			aq::ecs::EntityHandle playerHandle_;
		};
	}
}