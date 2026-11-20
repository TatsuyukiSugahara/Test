#pragma once
#ifdef AQ_DEBUG_IMGUI
#include "ECS/PrefabEditor.h"   // ecs::PrefabEditNode
#include "Level/LevelId.h"
#include "Util/SimpleJson.h"
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
		 * Level の編集モデル（エディタ全体設計 7.1 節・7.8 節「3.」）
		 *
		 * エディタが編集する正本。.level.json を PrefabEditNode のフォレストとサブLevel で表す。
		 * ワールドはここから作るプレビューで、置き換えは ProcessPreviewRequest がまとめて行う。
		 * LevelEditorPanel（と今後の Editor）が使う。
		 */
		class LevelEditModel
		{
		private:
			/** 編集内容（.level.json に対応） */
			std::string                                       name_ = "Level";
			std::vector<std::unique_ptr<ecs::PrefabEditNode>> entities_;   // 各要素 = 1 トップレベル entity ツリー
			std::vector<SubLevelEdit>                         subLevels_;

			/** 相対パス（Prefab・外部サブLevel）の解決基準。開いた / 保存したファイルのディレクトリ。新規では空 */
			std::string                                       baseDir_;

			/** ノード ID の通し番号。0 は未採番なので 1 から始める */
			uint32_t                                          nextEditId_ = 1;

			/** プレビューの状態 */
			bool                                              previewRequested_ = false;
			LevelId                                           previewLevelId_;   // 現在のプレビューの Level
			std::string                                       previewKey_;       // 現在のプレビューの in-memory 登録キー

			/** subLevels を再帰し、インライン定義の entities のルートへ AssignId を呼ぶ */
			void AssignIdsInSubLevels(std::vector<SubLevelEdit>& subLevels);


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
			ecs::PrefabEditNode* FindNode(uint32_t id);

			/** node とその子孫のうち、未採番（editId == 0）のノードへ ID を振る。モデルの中で一意 */
			void AssignId(ecs::PrefabEditNode& node);


		public:
			/** プレビューの置き換えを要求する（Load in World）。同じフレームの要求は 1 回にまとめる */
			void RequestPreview();

			/**
			 * 要求があればプレビューを置き換える（7.8 節「4.」）。要求が無ければ何もしない。
			 * System の反復の外で、1 フレームに 1 回呼ぶ（GPU の完了待ちと Flush を行うため）。
			 */
			void ProcessPreviewRequest();


		public:
			inline const std::string& GetName() const                { return name_; }
			inline void               SetName(const std::string& name) { name_ = name; }

			inline std::vector<std::unique_ptr<ecs::PrefabEditNode>>&       GetEntities()       { return entities_; }
			inline const std::vector<std::unique_ptr<ecs::PrefabEditNode>>& GetEntities() const { return entities_; }

			inline std::vector<SubLevelEdit>&       GetSubLevels()       { return subLevels_; }
			inline const std::vector<SubLevelEdit>& GetSubLevels() const { return subLevels_; }

			inline const std::string& GetBaseDir() const { return baseDir_; }

			/** 現在のプレビューの Level と in-memory 登録キー（プレビューが無ければ無効な LevelId と空文字列） */
			inline LevelId            GetPreviewLevelId() const { return previewLevelId_; }
			inline const std::string& GetPreviewKey() const     { return previewKey_; }
		};
	}
}
#endif
