#pragma once
#include "Scene.h"


namespace app
{
	namespace battle
	{
		class BattleScene : public app::IScene
		{
		public:
			BattleScene();
			~BattleScene();

			void Initialize() override;
			void Update() override;
			void Finalize() override;


		public:
			// TODO：Hash値などにする予定
			static constexpr uint32_t GetID() { return 1; }
		};
	}
}