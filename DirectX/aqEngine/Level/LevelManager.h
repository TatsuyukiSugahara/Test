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

		// ── メンバ変数 ──
		private:
			std::vector<LevelSlot>   slots_;
			std::vector<uint32_t>    freeList_;                 // 再利用可能な slot index
			std::vector<std::string> loadStack_;                // 再帰ロード中の正規化パス（循環サブLevel 検出）
			std::string              startupPath_;              // 起動時に読む Level（SetStartupLevel で設定）
			bool                     autoReload_ = false;       // D2: ファイル変更の自動監視
			float                    pollTimer_  = 0.0f;        // D2: ポーリング間引き用

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
		};
	}
}
