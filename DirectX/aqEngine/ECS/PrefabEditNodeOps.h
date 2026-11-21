#pragma once
#ifdef AQ_DEBUG_IMGUI
#include "PrefabEditor.h"
#include "Util/SimpleJson.h"
#include <memory>
#include <vector>


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


		/** ツリーのドラッグアンドドロップの荷物の種類名（荷物はノードのポインタ 1 個）。ルート列の見出しなどへ落とす先を作るときに使う */
		static constexpr const char* PREFAB_NODE_DRAG_PAYLOAD = "AQ_PREFAB_NODE";


		/** ツリーが返す操作要求の種類 */
		enum class PrefabNodeOpKind : uint8_t
		{
			Select,     // target を選択する
			AddChild,   // target の子を末尾に追加する
			Delete,     // target を削除する
			Move,       // target を newParent の子の末尾へ移す
		};


		/**
		 * ツリーが返す操作要求。ポインタは、同じフレームで適用するか ID に直すまでの間だけ有効。
		 */
		struct PrefabNodeOp
		{
			PrefabNodeOpKind kind;
			PrefabEditNode*  target    = nullptr;   // Select / AddChild の親 / Delete / Move の移すノード
			PrefabEditNode*  newParent = nullptr;   // Move の移動先
		};


		/** PrefabNodeDrawTree の描画オプション */
		struct PrefabTreeDrawOptions
		{
			const PrefabEditNode* selected      = nullptr;   // 選択中のノード（強調表示用）
			const PrefabEditNode* root          = nullptr;   // Delete を出さないノード（nullptr なら全ノード削除可）
			bool                  enableMove    = false;     // ドラッグアンドドロップで Move を出す
			bool                  allowRefChild = true;      // false なら参照ノードに Add Child を出さず、ドロップも受けない
			bool                  markReference = false;     // 参照ノードの名前に印を付ける
		};


		/**
		 * 階層ツリーを描画し、操作要求を ops へ積む。モデルは書き換えない。
		 * 自分や子孫の下へのドロップ、allowRefChild=false での参照ノードへのドロップは受けない。
		 */
		void PrefabNodeDrawTree(const PrefabEditNode* node, const PrefabTreeDrawOptions& options, const int depth,
			std::vector<PrefabNodeOp>& ops);


		/**
		 * ops をその場で適用する（Prefab エディタと Level エディタ用）。
		 * Select は選択を更新し、AddChild は子を追加して選択し、Delete は pendingDelete へ予約する
		 * （実際の除去は描画後に呼び出し側が行う）。Delete の対象か子孫が選択中なら選択を外す。Move は無視する。
		 * @param ensureTransform true = 追加した子ノードに TransformComponent を必須で付与する
		 */
		void PrefabNodeApplyOpsInPlace(const std::vector<PrefabNodeOp>& ops, PrefabEditNode*& selected,
			PrefabEditNode*& pendingDelete, const bool ensureTransform);


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
