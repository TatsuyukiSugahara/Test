#pragma once
#include "Entity.h"
#include "Util/SimpleJson.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace aq
{
	namespace ecs
	{
		// 解決済み・不変の生成プラン（ランタイム表現）。
		// components は { "Transform": {...}, "Decal": {...} } 形式の JsonValue オブジェクト
		// （overrides 適用後・ネスト参照展開後の最終形）。children はネスト済み。
		// shared_ptr<const PrefabData> として共有し、遅延生成コマンドが値捕獲する（設計 §4.3）。
		struct PrefabNodeData
		{
			std::string                 name;
			util::JsonValue             components = util::JsonValue::MakeObject();
			std::vector<PrefabNodeData> children;
		};

		// Prefab ツリー全体の不変プラン。root から子・孫が辿れる。
		struct PrefabData
		{
			PrefabNodeData root;
		};


		// PrefabData を共有保持する薄いハンドル。値コピーが安全（shared_ptr）。
		// 遅延生成コマンドは this ではなく内部の shared_ptr<const PrefabData> を値捕獲するため、
		// 一時 Prefab や Registry アンロード後の Flush でも use-after-free にならない（設計 §4.3）。
		class Prefab
		{
		public:
			Prefab() = default;
			explicit Prefab(std::shared_ptr<const PrefabData> data) : data_(std::move(data)) {}

			bool IsValid() const { return static_cast<bool>(data_); }
			const std::shared_ptr<const PrefabData>& Data() const { return data_; }

			// ツリー全体を 1 コマンドで遅延生成する（デフォルト）。ForEach / System Update 内からも安全。
			// 実体化は次の FlushCommands。parent が有効なら root をその子として親子付けする。
			// onComplete は生成完了時に root Entity を渡して呼ばれる（FlushCommands 内）。
			void Instantiate(
				EntityHandle                 parent     = EntityHandle(),
				std::function<void(Entity)>  onComplete = nullptr) const;

			// ツリー全体を即時生成し root Entity を返す（init / エディタ用・ForEach 外限定）。
			// 内部で iterationMutex_ をライトロックするため ForEach 中に呼ぶとデッドロックする。
			Entity InstantiateImmediate(EntityHandle parent = EntityHandle()) const;

		private:
			std::shared_ptr<const PrefabData> data_;
		};


		// 与えられた create プリミティブで 1 ツリー（root + 子孫）を生成する低レベル API。
		// Prefab::Instantiate / InstantiateImmediate はこれを onEachCreated=nullptr で呼ぶ薄いラッパ。
		// Level 層が「LevelMemberComponent 型を注入する create」+「levelId を差す onEachCreated」で
		// 木構築ロジックを再利用するために公開する（Level 設計 §7）。
		//   create        : 完全な TypeInfo 列から Entity を生成する（遅延 NoLock / 即時ロック版のいずれか）。
		//   onEachCreated : 各ノード Entity の生成・deserialize 完了直後に呼ばれる（省略可）。root/子/孫すべてで発火。
		// 戻り値は root Entity（失敗時は無効）。
		Entity InstantiatePrefabTree(
			const PrefabNodeData&                                     root,
			EntityHandle                                              parent,
			const std::function<Entity(std::vector<TypeInfo>)>&       create,
			const std::function<void(Entity, const PrefabNodeData&)>& onEachCreated = nullptr);
	}
}
