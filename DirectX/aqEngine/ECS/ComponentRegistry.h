#pragma once
#include "TypeInfo.h"
#include "Entity.h"
#include "Util/SimpleJson.h"
#include <functional>
#include <vector>
#include <utility>

// このレジストリのコア（typeName / serialize / deserialize / add / has / get / requiredWith）は
// 常時コンパイルする。リリースビルド（AQ_DEBUG_IMGUI 無効）でも JSON からコンポーネントを
// 復元できるようにするため。drawInspector（ImGui 編集）だけが AQ_DEBUG_IMGUI でのみ設定される。

namespace aq
{
	namespace ecs
	{
		// コンポーネントの Inspector 表示・生成・依存関係・シリアライズを記述するメタデータ。
		// remove は Step 6 (RemoveComponent ECS 対応後) まで保留。
		struct ComponentMeta
		{
			const char*                           displayName;
			const char*                           typeName = nullptr;  // JSON キー（serialize/deserialize 用）
			std::vector<TypeInfo>                 requiredWith;   // UI 表示 + Remove 側バリデーション用
			std::function<bool(EntityHandle)>     has;
			std::function<void*(EntityHandle)>    get;
			std::function<void(EntityHandle)>     add;            // 依存コンポーネントも含めて自己完結
			std::function<void(EntityHandle)>     drawInspector;  // AQ_DEBUG_IMGUI のみ設定（release では空）
			std::function<void(EntityHandle)>     remove;         // nullptr = Inspector から削除不可

			// コンポーネント1つ分を JSON オブジェクトへ書き出す / から読み込む。
			// nullptr = 当該コンポーネントは未対応（Reflect 化されていない）。
			std::function<void(EntityHandle, util::JsonValue&)>       serialize;
			std::function<void(EntityHandle, const util::JsonValue&)> deserialize;
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
			// 二重呼び出し防止済み。Application::Initialize() から（ビルド構成を問わず）一度だけ呼ぶこと。
			static void RegisterCoreComponents();

		private:
			std::vector<std::pair<TypeInfo, ComponentMeta>> entries_;
			bool coreRegistered_ = false;
		};
	}
}
