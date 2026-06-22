#pragma once
#ifdef AQ_DEBUG_IMGUI
#include "TypeInfo.h"
#include "Entity.h"
#include <functional>
#include <vector>
#include <utility>

namespace aq
{
	namespace ecs
	{
		// コンポーネントの Inspector 表示・生成・依存関係を記述するメタデータ。
		// remove は Step 6 (RemoveComponent ECS 対応後) まで保留。
		struct ComponentMeta
		{
			const char*                           displayName;
			std::vector<TypeInfo>                 requiredWith;   // UI 表示 + Remove 側バリデーション用
			std::function<bool(EntityHandle)>     has;
			std::function<void*(EntityHandle)>    get;
			std::function<void(EntityHandle)>     add;            // 依存コンポーネントも含めて自己完結
			std::function<void(EntityHandle)>     drawInspector;  // EntityHandle 経由で GetComponent
			std::function<void(EntityHandle)>     remove;         // nullptr = Inspector から削除不可
		};


		// ComponentMeta の中央レジストリ。
		// Application::Initialize() から RegisterCoreComponents() を一度だけ呼ぶこと。
		class ComponentRegistry
		{
		public:
			static ComponentRegistry& Get();

			// 二重登録は無視する。
			void Register(TypeInfo typeInfo, ComponentMeta meta);

			// 型ハッシュで ComponentMeta を引く。見つからなければ nullptr。
			const ComponentMeta* Find(size_t typeHash) const;

			// 全登録 ComponentMeta を返す（Add Component パレット等で使用）。
			const std::vector<std::pair<TypeInfo, ComponentMeta>>& GetAll() const { return entries_; }

			// エンジンコアコンポーネントを一括登録する。
			// 二重呼び出し防止済み。Application::Initialize() の #ifdef AQ_DEBUG_IMGUI ブロックから呼ぶ。
			static void RegisterCoreComponents();

		private:
			std::vector<std::pair<TypeInfo, ComponentMeta>> entries_;
			bool coreRegistered_ = false;
		};
	}
}
#endif
