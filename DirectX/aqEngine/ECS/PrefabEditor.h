#pragma once
#ifdef AQ_DEBUG_IMGUI
#include "Core/IDebugRenderable.h"
#include "AlignedStorage.h"
#include "Util/SimpleJson.h"
#include <memory>
#include <string>
#include <vector>

namespace aq
{
	namespace ecs
	{
		// 参照ノードの override コンポーネントを読み込んだ出どころ（エディタ全体設計 7.8 節「2. override の保持」）。
		enum class PrefabOverrideSource : uint8_t
		{
			None,              // 実体ノードのコンポーネント / 新しく足したコンポーネント
			Components,        // overrides.components（deep-merge）
			AddedComponents,   // overrides.addedComponents（後から丸ごと上書き）
		};

		// エディタ用ワーキングコピー（設計 §6.2）。ランタイムの PrefabData とは別物。
		// コンポーネントは型消去した実体（AlignedStorage）として保持し、既存 Inspector を再利用する。
		struct PrefabEditComponent
		{
			AlignedStorage       data;                                // 型は data.Type()
			util::JsonValue      loadedJson;                          // 参照ノードの override: 読み込んだ直後の serializePtr の値。新しく足したものは空（Null）
			PrefabOverrideSource source = PrefabOverrideSource::None; // 参照ノードの override: 読み込んだ出どころ
		};

		struct PrefabEditNode
		{
			std::string                                  name = "Node";
			std::string                                  prefabRef;          // 非空 = .prefab.json 参照ノード（実体はロード時に解決）
			std::vector<PrefabEditComponent>             components;         // 通常=インライン / 参照ノード=override コンポーネント（deep-merge/追加）
			std::vector<std::string>                     removedComponents;  // 参照ノードの override: 参照先から除去する typeName
			std::vector<std::unique_ptr<PrefabEditNode>> children;           // ポインタ安定のため unique_ptr
			util::JsonValue                              overridesRaw = util::JsonValue::MakeObject();   // 参照ノード: 読み込んだ overrides の生 JSON（無ければ空のオブジェクト）。書き出しはこれを写して始める
			uint32_t                                     editId = 0;        // 保存しない。LevelEditModel::AssignId が振る（0 = 未採番）。Prefab エディタは使わない
		};


		// ImGui で Prefab を作成・編集・保存・ロード・プレビュー生成するパネル（設計 §6.3）。
		// Reflect を単一の真実として、エディタ・シリアライザ・ランタイム生成が同じ経路を共有する。
		class PrefabEditorPanel : public IDebugRenderable
		{
		public:
			PrefabEditorPanel();

			const char* GetDebugLabel() const override { return "Prefab Editor"; }
			const char* GetDebugCategory() const override { return "Tools"; }
			void        DebugRenderMenu() override;
			void        DebugRender()     override;

		private:
			void DrawTree(PrefabEditNode* node, int depth);
			void DrawInspector();
			void DrawToolbar();

			// target を親の children から再帰的に探して除去する。除去したら true。
			bool RemoveNode(PrefabEditNode* parent, PrefabEditNode* target);

			// 編集ツリー ⇄ JSON。
			util::JsonValue                 NodeToJson(const PrefabEditNode& node) const;
			std::unique_ptr<PrefabEditNode> JsonToNode(const util::JsonValue& json) const;

			void NewPrefab();
			void Save();
			void Load();
			void SpawnPreview();   // 現在の編集ツリーをランタイム Prefab 化してシーンへ遅延生成

			std::unique_ptr<PrefabEditNode> root_;
			PrefabEditNode*                 selected_     = nullptr;
			PrefabEditNode*                 pendingDelete_ = nullptr;   // 描画後に除去するノード
			bool                            show_         = false;
			char                            pathBuf_[260] = "Assets/Prefabs/New.prefab.json";
		};
	}
}
#endif
