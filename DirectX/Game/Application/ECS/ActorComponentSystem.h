#pragma once
#include "Actor/StateMachine.h"


namespace app
{
	namespace ecs
	{
		/**
		 * ステートマシン機能
		 */
		class StateMachineComponent : public aq::ecs::IComponent
		{
			ecsComponent(app::ecs::StateMachineComponent);

		private:
			// StateMachine は各ステートがアドレスを保持するため動かせない。
			// unique_ptr で持てば、Chunk の拡張でコンポーネントを移してもアドレスは変わらない。
			// (デストラクタは宣言しない。宣言すると暗黙の move が消えてコピーになる)
			std::unique_ptr<app::actor::StateMachine> stateMachine_ = std::make_unique<app::actor::StateMachine>();


		public:
			StateMachineComponent() {}


		public:
			app::actor::StateMachine* GetStateMachine() { return stateMachine_.get(); }
		};




		/**
		 * ステートマシン機能用のシステム
		 */
		class ActorStateMachineSystem : public aq::ecs::SystemBase
		{
		public:
			ActorStateMachineSystem();
			~ActorStateMachineSystem();

			void Update() override;
		};
	}
}
