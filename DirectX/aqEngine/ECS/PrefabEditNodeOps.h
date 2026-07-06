#pragma once
#ifdef AQ_DEBUG_IMGUI
#include "PrefabEditor.h"
#include "Util/SimpleJson.h"
#include <memory>


namespace aq
{
	namespace ecs
	{
		/**
		 * PrefabEditNode（型消去コンポーネント付きの編集ノード）の共有編集オペレーション。
		 * Prefab エディタと Level エディタが同じツリー編集 UX / JSON 往復を共有するために切り出す。
		 * すべて ComponentRegistry の serializePtr / deserializePtr / drawInspectorPtr を経路とする。
		 */

		/** 編集ノード → JSON。prefabRef が非空なら { name, prefab } の参照ノードとして書き出す。 */
		util::JsonValue PrefabNodeToJson(const PrefabEditNode& node);


		/** JSON → 編集ノード。"prefab" があれば参照ノード（prefabRef）として保持する（未登録 typeName はスキップ）。 */
		std::unique_ptr<PrefabEditNode> PrefabNodeFromJson(const util::JsonValue& json);


		/** ノードに TransformComponent が無ければ追加する（Level エディタの Transform 必須化用）。 */
		void PrefabNodeEnsureTransform(PrefabEditNode& node);


		/**
		 * 階層ツリーを描画する。選択と削除予約は参照で更新する。
		 * @param root           「Delete 不可」判定に使うルート（nullptr なら全ノード削除可）
		 * @param ensureTransform true = 追加した子ノードに TransformComponent を必須で付与する
		 */
		void PrefabNodeDrawTree(PrefabEditNode* node, PrefabEditNode*& selected,
			PrefabEditNode*& pendingDelete, const PrefabEditNode* root, const int depth,
			const bool ensureTransform);


		/**
		 * 選択ノードのインスペクター（参照・名前・コンポーネント追加/削除/編集・子追加）。
		 * @param ensureTransform true = 追加した子ノードに TransformComponent を必須で付与する
		 */
		void PrefabNodeDrawInspector(PrefabEditNode& node, PrefabEditNode*& selected, const bool ensureTransform);


		/** parent 以下から target を再帰的に外す。外したら true。 */
		bool PrefabNodeRemove(PrefabEditNode* parent, PrefabEditNode* target);
	}
}
#endif
