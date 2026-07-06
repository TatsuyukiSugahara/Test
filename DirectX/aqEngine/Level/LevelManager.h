#pragma once
#include "Level/LevelId.h"
#include "Level/LevelData.h"
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>


namespace aq
{
	namespace level
	{
		// 非同期（フレーム分割）ロードの進捗。LevelManager が毎フレーム更新する（設計 §15 段階1）。
		struct LevelLoadProgress
		{
			LevelId  id;
			uint32_t total = 0;   // 生成予定エンティティ総数（サブLevel 含む）
			uint32_t built = 0;   // 生成済み
			bool     done  = false;
		};


		// LoadAsync の戻り値。進捗を live に読める薄いハンドル（shared_ptr で共有）。
		// ローディング画面はこれを毎フレーム見て Progress()/IsDone() で遷移する。
		class LevelLoadHandle
		{
		private:
			std::shared_ptr<const LevelLoadProgress> progress_;

		public:
			LevelLoadHandle() = default;
			explicit LevelLoadHandle(std::shared_ptr<const LevelLoadProgress> p) : progress_(std::move(p)) {}

			bool     IsValid()    const { return static_cast<bool>(progress_); }
			LevelId  GetLevelId() const { return progress_ ? progress_->id : LevelId(); }
			uint32_t Total()      const { return progress_ ? progress_->total : 0; }
			uint32_t Built()      const { return progress_ ? progress_->built : 0; }
			bool     IsDone()     const { return !progress_ || progress_->done; }
			float    Progress()   const { return (progress_ && progress_->total) ? static_cast<float>(progress_->built) / static_cast<float>(progress_->total) : 1.0f; }
		};


		// ロード済み Level のツリーを所有し、Load/Unload の実体を担うシングルトン。
		// entities は単一グローバル EntityContext のワールドへ生成し、各 Entity に LevelMemberComponent
		// を付与して所属を記録する（設計 §1・§7）。
		class LevelManager
		{
		// ── 内部型 ──
		private:
			struct LevelSlot
			{
				std::string                      path;
				std::shared_ptr<const LevelData> data;
				LevelId                          parent;
				std::vector<uint32_t>            children;      // 子 Level の slot index（Unload カスケード用）
				uint32_t                         generation = 0;
				bool                             loaded     = false;
				int64_t                          fileTime   = 0; // ファイル由来 root の最終更新時刻（変更検知用・非ファイルは 0）
			};

			// 非同期ロードの 1 エンティティ生成ジョブ（フラット化・親→子順）。
			struct BuildJob
			{
				const ecs::PrefabNodeData* node           = nullptr;   // 生成元ノード（keepAlive が生存保証）
				int32_t                    parentJobIndex = -1;        // 同一 Level フォレスト内の親ジョブ（-1 = ルート）
				LevelId                    levelId;                     // このエンティティの所属 Level
				ecs::EntityHandle          handle;                      // 生成後に埋まる
			};

			// 進行中の非同期ロード 1 件。
			struct AsyncLoad
			{
				std::vector<BuildJob>                         jobs;
				size_t                                        cursor           = 0;
				uint32_t                                      entitiesPerFrame = 64;
				std::shared_ptr<LevelLoadProgress>            progress;
				std::vector<std::shared_ptr<const LevelData>> keepAlive;   // node ポインタの生存保証
			};

		// ── メンバ変数 ──
		private:
			std::vector<LevelSlot>   slots_;
			std::vector<uint32_t>    freeList_;                 // 再利用可能な slot index
			std::vector<std::string> loadStack_;                // 再帰ロード中の正規化パス（循環サブLevel 検出）
			std::string              startupPath_;              // 起動時に読む Level（SetStartupLevel で設定）
			bool                     autoReload_ = false;       // D2: ファイル変更の自動監視
			float                    pollTimer_  = 0.0f;        // D2: ポーリング間引き用
			std::vector<AsyncLoad>   asyncLoads_;               // §15: 進行中の非同期ロード

		// ── メンバ関数 ──
		public:
			static LevelManager& Get();

			// プログラムから起動時に読み込む Level を指定する（正規化して保持するだけ・ロードはしない）。
			void SetStartupLevel(std::string_view pathOrId);

			// SetStartupLevel で指定した Level をロードする。未指定なら無効な LevelId を返す。
			LevelId LoadStartup();

			// pathOrId の Level をロードする（entities を遅延生成）。parent が有効なら Level ツリーの子にする。
			// 実体生成は次の FlushCommands。成功時はロード済み LevelId、失敗時は無効な LevelId を返す。
			// ※ L2: entities のみ。subLevels の再帰ロードは L4。
			LevelId Load(std::string_view pathOrId, LevelId parent = LevelId());

			// pathOrId の Level を非同期（フレーム分割）ロードする（設計 §15 段階1）。entitiesPerFrame ずつ
			// EntityContext::Update 後の安全点（Tick）で生成する。loadOnStart サブLevel も同じキューに含める。
			// 進捗は返り値のハンドルで live に読める（ローディング画面用）。実体は複数フレームにわたり生成される。
			LevelLoadHandle LoadAsync(std::string_view pathOrId, LevelId parent = LevelId(), const uint32_t entitiesPerFrame = 64);

			// 進行中の非同期ロード件数（デバッグ表示用）。
			size_t GetActiveAsyncCount() const { return asyncLoads_.size(); }

			// Level をアンロードする。配下の全 Entity を遅延破棄（RequestDestroyEntity）し、
			// サブLevel も再帰的に破棄する。LevelId は generation を進めて stale 化する。
			// 実際の Entity 破棄は次の FlushCommands。
			void Unload(LevelId id);

			// ロード済みの全 Level をアンロードする（root から再帰）。
			void UnloadAll();

			// LevelId が現在ロード済みか（generation 一致を含む）。
			bool IsLoaded(LevelId id) const;

			// path から現在ロード済みの LevelId を引く。未ロードなら無効な LevelId。
			LevelId Find(std::string_view pathOrId) const;

			// D1: 参照キャッシュ（Prefab/Level レジストリ）を捨て、ファイル由来の全 root Level を作り直す（手動リロード）。
			void ReloadAll();

			// D2: ファイル変更の自動監視の ON/OFF。
			void SetAutoReload(const bool enabled);
			bool IsAutoReload() const;

			// D2: 毎フレーム呼ぶ。autoReload_ が有効なら間引いてファイル mtime を確認し、変更を検知したら reload する。
			void Tick(const float dt);

		private:
			LevelManager() = default;

			// 空き slot を確保して LevelId を採番する。parent が有効ならその children に登録する。
			LevelId AllocateSlot(std::string path, std::shared_ptr<const LevelData> data, LevelId parent);

			// data->entities のフォレストを 1 コマンドで遅延生成し、各 Entity へ levelId を差す。
			void InstantiateEntities(const std::shared_ptr<const LevelData>& data, LevelId levelId);

			// インライン定義のサブLevel をロードする（slot 採番 + entities 生成 + サブLevel 再帰）。
			// ファイルを介さないため loadStack_（パス循環検出）は使わない。
			LevelId LoadInline(std::shared_ptr<const LevelData> data, LevelId parent);

			// index の現在 generation で LevelId を作る（内部用・呼び出し側で範囲確認済みのこと）。
			LevelId MakeId(uint32_t index) const;

			// id とその全子孫（children 再帰）の LevelId を out へ収集する（DFS）。
			void CollectSubtree(LevelId id, std::vector<LevelId>& out) const;

			// slot を解放し generation を進める（freelist へ返す）。stale 化。
			void FreeSlot(LevelId id);

			// 親 Level の children リストから id を外す。
			void DetachFromParent(LevelId id);

			// key がファイルパスか（"<inline>" / "<...preview...>" などの合成キーでないか）。
			static bool IsFileKey(const std::string& key);

			// path の最終更新時刻を取得する。取得失敗は 0。
			int64_t QueryFileTime(const std::string& path) const;

			// ファイル由来 root の mtime を確認し、変更されていた Level を作り直す（D2 本体）。
			void PollFileChanges();

			// 非同期ロードを 1 フレーム分進める（Tick から呼ぶ・安全点前提で即時生成する）。
			void ProcessAsyncLoads();

			// LevelData フォレスト（+ loadOnStart サブLevel）を BuildJob 列にフラット化し、slot も採番する。
			void FlattenLevel(const std::shared_ptr<const LevelData>& data, LevelId levelId, AsyncLoad& load, const int depth);
			void FlattenNode(const ecs::PrefabNodeData& node, LevelId levelId, const int32_t parentJobIndex, AsyncLoad& load);
		};
	}
}
