#pragma once
#include "Prefab.h"
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

namespace aq
{
	namespace ecs
	{
		// ランタイムのキャッシュキー（0 = 無効）。
		// 正本は path / GUID 文字列であり、これは保存しない（設計 §8.1）。
		struct PrefabId
		{
			uint64_t value = 0;
			bool IsValid() const { return value != 0; }
		};


		// path（または GUID 文字列）を正本に PrefabData を解決・キャッシュするレジストリ。
		// 文字列 → uint64 キーは内部生成し、衝突は検出して別キーを割り当てる（設計 §8.1）。
		class PrefabRegistry
		{
		public:
			static PrefabRegistry& Get();

			// path（または GUID 文字列）を解決して PrefabId を返す。
			// 既に解決済みならキャッシュを返す。未ロードなら PrefabSerializer::Load で読み込む。
			// 失敗時は無効な PrefabId（value==0）。
			PrefabId Resolve(std::string_view pathOrGuid);

			// 構築済み Prefab を文字列キーで登録する（ファイルを介さない動的生成・テスト用）。
			// 既存キーがあればその PrefabId を返す（再登録しない）。
			PrefabId Register(std::string_view key, const Prefab& prefab);

			// PrefabId から不変プランを取得する。未登録なら nullptr。
			// 設計 §4.3: 取得した shared_ptr を Instantiate が値捕獲して寿命を保証する。
			std::shared_ptr<const PrefabData> Find(PrefabId id) const;

			// キャッシュを全消去する（シーン切替・ホットリロード用）。
			void Clear();

		private:
			PrefabRegistry() = default;

			// path 正規化（バックスラッシュ→スラッシュ）。大文字小文字は変更しない。
			static std::string Normalize(std::string_view pathOrGuid);
			// 正規化済みキー → uint64（FNV-1a 64bit）。
			static uint64_t HashKey(const std::string& normalized);

			// 衝突回避: key が別の文字列に割り当て済みなら線形プローブで空きキーを探す。
			uint64_t AssignKey(const std::string& normalized);

			std::unordered_map<std::string, PrefabId>                   keyToId_;   // 正規化文字列 → id
			std::unordered_map<uint64_t, std::string>                   idToKey_;   // 衝突検出 + 診断用
			std::unordered_map<uint64_t, std::shared_ptr<const PrefabData>> idToData_;
		};
	}
}
