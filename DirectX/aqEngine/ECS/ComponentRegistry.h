#pragma once
#include "TypeInfo.h"
#include "Entity.h"
#include "Util/SimpleJson.h"
#include <functional>
#include <vector>
#include <utility>
#include <string_view>

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

			// --- 型消去（Prefab エディタ用・設計 §6.2/§6.3）---
			// AlignedStorage::Get() の void* に対して Reflect を回す。Reflect 化済みコンポーネントのみ設定。
			// 構築/破棄は AlignedStorage が TypeInfo 経由で行うため construct/destruct は持たない。
			std::function<void(void*, util::JsonValue&)>             serializePtr;
			std::function<void(void*, const util::JsonValue&)>       deserializePtr;
			std::function<void(void*)>                              drawInspectorPtr;  // AQ_DEBUG_IMGUI のみ
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

			// JSON キー（typeName）で ComponentMeta を引く。見つからなければ nullptr。
			// Prefab 生成で components のキー → メタ（deserialize 等）を解決するのに使う。
			const ComponentMeta* Find(std::string_view typeName) const;

			// JSON キー（typeName）→ TypeInfo を解決する。
			// 見つからなければ既定の TypeInfo（GetHash() == size_t(-1)、TypeInfo() と == 比較で判定）を返す。
			TypeInfo TypeOf(std::string_view typeName) const;

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
