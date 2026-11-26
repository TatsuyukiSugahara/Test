#pragma once
#ifdef AQ_DEBUG_IMGUI
#include "ECS/PrefabEditor.h"   // ecs::PrefabEditNode
#include "ECS/Entity.h"         // ecs::EntityHandle
#include "Level/LevelId.h"
#include "Util/SimpleJson.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>


namespace aq
{
	namespace level
	{
		/**
		 * サブLevel の編集エントリ（.level.json の subLevels[] の 1 要素）
		 *
		 * インライン定義は自分の subLevels を持てる（ランタイムの LevelSerializer と同じく再帰で読み書きする）。
		 * UI は 1 段目だけを編集し、2 段目以降は表示しない（保存では保持する）。
		 */
		struct SubLevelEdit
		{
			bool                                              isInline = false;   // true = その場で作るインライン定義
			std::string                                       path;               // isInline=false: 外部 .level.json パス
			std::string                                       name = "SubLevel";  // isInline=true: 名前
			std::vector<std::unique_ptr<ecs::PrefabEditNode>> entities;           // isInline=true: 配下 entity ツリー
			std::vector<SubLevelEdit>                         subLevels;          // isInline=true: 入れ子のサブLevel
			bool                                              loadOnStart = true;
		};




		/**
		 * ノードの置き場所
		 *
		 * nodeId != 0 ならそのノードの子。0 なら subLevelIndex の列（-1 = トップレベルの entities、
		 * 0 以上 = 1 段目のインライン subLevel のルート列）。nodeId != 0 のとき subLevelIndex は無視する
		 * （FindParent / DetachNode が返す値は -1 にそろえる）。UI と同じく 2 段目以降の subLevel は扱わない。
		 */
		struct NodeParent
		{
			uint32_t nodeId        = 0;
			int      subLevelIndex = -1;
		};


		/** プレビューのノードとエンティティの対応表（中身は .cpp。エディタ E11 5.） */
		struct PreviewEntityTable;
		struct LevelData;


		/** index に渡すと列の末尾を指す */
		static constexpr size_t APPEND = SIZE_MAX;




		/**
		 * Level の編集モデル（エディタ全体設計 7.1 節・7.8 節「3.」）
		 *
		 * エディタが編集する正本。.level.json を PrefabEditNode のフォレストとサブLevel で表す。
		 * ワールドはここから作るプレビューで、置き換えは ProcessPreviewRequest がまとめて行う。
		 * LevelEditorPanel（と今後の Editor）が使う。
		 */
		class LevelEditModel
		{
		public:
			using NodeParent = level::NodeParent;
			static constexpr size_t APPEND = level::APPEND;


		private:
			/** 編集内容（.level.json に対応） */
			std::string                                       name_ = "Level";
			std::vector<std::unique_ptr<ecs::PrefabEditNode>> entities_;   // 各要素 = 1 トップレベル entity ツリー
			std::vector<SubLevelEdit>                         subLevels_;

			/** 相対パス（Prefab・外部サブLevel）の解決基準。開いた / 保存したファイルのディレクトリ。新規では空 */
			std::string                                       baseDir_;

			/** 開いたファイルの先頭のコメント（ルートの値より前の // の行）。保存で先頭に書き戻す。新規では空 */
			std::string                                       leadingComments_;

			/** ノード ID の通し番号。0 は未採番なので 1 から始める */
			uint32_t                                          nextEditId_ = 1;

			/** プレビューの状態 */
			bool                                              previewRequested_ = false;
			LevelId                                           previewLevelId_;   // 現在のプレビューの Level
			std::string                                       previewKey_;       // 現在のプレビューの in-memory 登録キー

			/**
			 * プレビューのノードとエンティティの対応表（エディタ E11 5.）。
			 * LevelManager の生成の観測の関数もこれを共有で持つ（モデルが先に破棄されても観測の関数が壊れた先を指さないように）。
			 * 中のノードのポインタはプレビューの登録の間（Register から Unregister まで）だけ有効なので、ReleasePreview で空にする
			 */
			std::shared_ptr<PreviewEntityTable>               previewTable_;
			bool                                              spawnObserverSet_ = false;   // LevelManager に観測の関数を設定している

			/** subLevels を再帰し、インライン定義の entities のルートへ AssignId を呼ぶ */
			void AssignIdsInSubLevels(std::vector<SubLevelEdit>& subLevels);

			/** data（このモデルの ToJson から作ったもの）とモデルを並べて歩き、ノード → editId の表を作って生成の観測を始める */
			void BeginPreviewEntityTable(const LevelData& data);

			/** 生成の観測を外し、対応表を空にする */
			void EndPreviewEntityTable();


		public:
			LevelEditModel();


		public:
			/** 新規 Level にする（空のエンティティを 1 つ用意する）。基準ディレクトリは空にする */
			void NewLevel();

			/** ファイルを読み込む。失敗したら false で、モデルは変えない。成功したら基準ディレクトリをそのファイルのディレクトリにする */
			bool LoadFile(const std::string& path);

			/** ファイルへ保存する。一時ファイルに書いてから置き換える（7.7 節）。成功したら基準ディレクトリをそのファイルのディレクトリにする */
			bool SaveFile(const std::string& path);

			/** 編集モデル → .level.json 相当の JSON */
			util::JsonValue ToJson() const;

			/** .level.json 相当の JSON → 編集モデル。ルートがオブジェクトでなければ false で、モデルは変えない */
			bool FromJson(const util::JsonValue& root);


		public:
			/**
			 * ノード ID（保存しない。読み込み・新規・ノード追加のたびに振る。モデルの中で一意。7.8 節「3.」）。
			 * トップレベルの entities と、インラインのサブ Level の entities（入れ子も含む）の両方を対象にする。
			 */

			/** id を持つノードを探す（無ければ nullptr）。トップレベルとインラインのサブ Level の entities を探す */
			ecs::PrefabEditNode*       FindNode(uint32_t id);
			const ecs::PrefabEditNode* FindNode(uint32_t id) const;

			/** node とその子孫のうち、未採番（editId == 0）のノードへ ID を振る。モデルの中で一意 */
			void AssignId(ecs::PrefabEditNode& node);

			/**
			 * ノードの親と、親の列の中での位置を調べる。1 段目までの列（トップレベルと 1 段目のインライン subLevel）にある
			 * ノードだけが対象で、見つからない・2 段目以降の subLevel の中なら false
			 */
			bool FindParent(uint32_t id, NodeParent& outParent, size_t& outIndex) const;

			/**
			 * parent の index（範囲外は末尾）へ node を挿入する。node とその子孫のうち未採番のものだけに ID を振り、
			 * 採番済みの ID は変えない。挿入したノードの ID を返す。
			 * 参照ノードの下、ファイル参照の subLevel、存在しない親・範囲外の subLevel へは挿入できない（0 を返し、モデルは変えない）。
			 */
			uint32_t InsertNode(const NodeParent& parent, size_t index, std::unique_ptr<ecs::PrefabEditNode> node);

			/**
			 * id のノードをサブツリーごと取り外して返す（editId は子孫も含めて保つ）。無ければ nullptr でモデルは変えない。
			 * outParent / outIndex には取り外す前の置き場所を返す
			 */
			std::unique_ptr<ecs::PrefabEditNode> DetachNode(uint32_t id, NodeParent* outParent = nullptr, size_t* outIndex = nullptr);

			/**
			 * id のノードをサブツリーごと newParent の index（範囲外は末尾）へ移す。
			 * 同じ列の中で後ろへ動かすときの index は、取り外した後の添字として扱う。
			 * 参照ノードの下・自分と子孫の下・ファイル参照の subLevel へは移せない（false でモデルは変えない）
			 */
			bool MoveNode(uint32_t id, const NodeParent& newParent, size_t index);

			/** id のノードの名前を変える。ノードが無ければ false */
			bool SetNodeName(uint32_t id, const std::string& name);

			/**
			 * 参照ノードの prefab とファイル参照の subLevel の path のうち、絶対パスでないものがあれば true。
			 * 絶対の判定は PrefabSerializer の JoinPath と同じ（区切りを '/' にそろえて、先頭が '/' か 2 文字目が ':'）。
			 * トップレベル・子孫・インライン subLevel（入れ子を含む）のすべてを調べる
			 */
			bool HasRelativeReferences() const;


		public:
			/**
			 * コンポーネントの値の操作。ノードは editId で指す。
			 * 失敗（false）のときはモデルを変えない。
			 */

			/**
			 * 参照ノードの解決済みのコンポーネント（{ typeName: JSON }）を outComponents へ返す。
			 * PrefabSerializer::FromJson(PrefabNodeToJson(node), baseDir) の根の components と同じ（参照先の展開と override の適用を実行時と同じ経路で行う）。
			 * 参照ノードでない・直接の参照先のファイルを読めないときは false（error に理由）。入れ子の参照の失敗はログだけで、結果はそのまま返す。
			 * ファイルを読むので、毎フレームは呼ばない
			 */
			bool ResolveReferenceComponents(uint32_t id, util::JsonValue& outComponents, std::string* error = nullptr) const;

			/**
			 * typeName のコンポーネントの key だけを value に変える（deserializePtr に { key: value } を渡す）。
			 * 実体ノードはそのコンポーネントを持つときだけ。参照ノードは、そのコンポーネントを初めて操作するときに
			 * 基準を解決済みの値へ付け替えてから適用する（解決先に型が無い・解決に失敗したら false）。
			 * 未登録の型・serializePtr / deserializePtr の無い型も false
			 */
			bool SetComponentField(uint32_t id, const std::string& typeName, const std::string& key, const util::JsonValue& value);

			/**
			 * SetComponentField が書き換える前の値（typeName のコンポーネントの key の JSON）を outValue へ返す。
			 * 実体ノードは持っているコンポーネントを serializePtr した値の key。参照ノードは、付け替え済み・新しく足した
			 * （loadedJson が空）ものはそのまま、まだなら「既定値 ← 解決済みの値」を serializePtr した値の key。
			 * SetComponentField が false を返す条件（型が無い、removedComponents にある、解決できない）と、key が無いときは false。
			 * モデルは変えない（付け替えもしない）。参照ノードで未付け替えのときはファイルを読むので、毎フレームは呼ばない
			 */
			bool GetComponentField(uint32_t id, const std::string& typeName, const std::string& key, util::JsonValue& outValue) const;

			/**
			 * 実体ノードへコンポーネントを足す（json があれば deserializePtr）。既に持つ・未登録・参照ノードなら false。
			 * index はコンポーネントの列の中の位置（既定と範囲外は末尾）
			 */
			bool AddComponent(uint32_t id, const std::string& typeName, const util::JsonValue* json = nullptr, size_t index = APPEND);

			/**
			 * 実体ノードからコンポーネントを外す。addable == false の型（Transform）・持っていない・参照ノードなら false。
			 * outJson には外す前の serializePtr の値を、outIndex には外す前のコンポーネントの列の中の位置を返す
			 */
			bool RemoveComponent(uint32_t id, const std::string& typeName, util::JsonValue* outJson = nullptr, size_t* outIndex = nullptr);


		public:
			/** 完了待ち・Flush・前回プレビューのアンロード・Flush・登録の解除を行う（新しいプレビューは作らない） */
			void ReleasePreview();


		public:
			/** プレビューの置き換えを要求する（Load in World）。同じフレームの要求は 1 回にまとめる */
			void RequestPreview();

			/**
			 * 要求があればプレビューを置き換える（7.8 節「4.」）。要求が無ければ何もしない。
			 * System の反復の外で、1 フレームに 1 回呼ぶ（GPU の完了待ちと Flush を行うため）。
			 */
			void ProcessPreviewRequest();

			/**
			 * 保留中のプレビュー要求を取り消し、いまのプレビューを片付ける（新しいプレビューは作らない）。
			 * ProcessPreviewRequest の「完了待ち・Flush・アンロード・Flush・登録の解除」だけを行うので、
			 * ProcessPreviewRequest と同じく System の反復の外で呼ぶ。
			 * @return プレビューがあって片付けたら true
			 */
			bool ClearPreview();


		public:
			/**
			 * プレビューのノードとエンティティの対応表（エディタ E11 5.）。プレビューを作る（ProcessPreviewRequest）ときに作り、
			 * 次の Flush でエンティティが生成されたときに埋まり、ReleasePreview で空になる。
			 * 対象はトップレベルと 1 段目のインライン subLevel のノード（UI と同じ）。参照ノードは展開された部分木のすべての
			 * エンティティがその参照ノードの editId に結ばれる。
			 */

			/** editId のノードから生成されたエンティティの列（先頭がそのノードの根）。無ければ nullptr */
			const std::vector<ecs::EntityHandle>* FindPreviewEntities(uint32_t editId) const;

			/** entity を生成したノードの editId。対応表に無ければ 0 */
			uint32_t FindEditIdOfEntity(ecs::EntityHandle entity) const;


		public:
			inline const std::string& GetName() const                { return name_; }
			inline void               SetName(const std::string& name) { name_ = name; }

			inline std::vector<std::unique_ptr<ecs::PrefabEditNode>>&       GetEntities()       { return entities_; }
			inline const std::vector<std::unique_ptr<ecs::PrefabEditNode>>& GetEntities() const { return entities_; }

			inline std::vector<SubLevelEdit>&       GetSubLevels()       { return subLevels_; }
			inline const std::vector<SubLevelEdit>& GetSubLevels() const { return subLevels_; }

			inline const std::string& GetBaseDir() const { return baseDir_; }

			/** 開いたファイルの先頭のコメント（無ければ空）。保存で先頭に書き戻す */
			inline const std::string& GetLeadingComments() const { return leadingComments_; }

			/** 現在のプレビューの Level と in-memory 登録キー（プレビューが無ければ無効な LevelId と空文字列） */
			inline LevelId            GetPreviewLevelId() const { return previewLevelId_; }
			inline const std::string& GetPreviewKey() const     { return previewKey_; }
		};
	}
}
#endif
