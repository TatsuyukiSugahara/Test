#pragma once


namespace aq
{
	namespace level
	{
		// Level 層のコンポーネント（LevelStreamComponent 等）を ecs::ComponentRegistry へ登録する。
		// JSON シリアライズ / Prefab・Level への配置 / Inspector 編集を可能にする。
		// ecs::ComponentRegistry::RegisterCoreComponents() の後に、Application::Initialize() から
		// 一度だけ呼ぶこと（ビルド構成を問わず）。
		//
		// ※ ECS 層に Level を依存させないため、登録は ECS ではなく Level 側から行う。
		void RegisterLevelComponents();
	}
}
