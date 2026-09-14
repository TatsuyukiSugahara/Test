#include "aq.h"
#include "Resource/AssetPath.h"
#include <cctype>
#include <filesystem>


namespace aq
{
	namespace res
	{
		namespace
		{
			// 解決失敗ログに並べる候補の上限。StartupMarkf のバッファは 1 行 480 バイトで、
			// 候補は拡張子バリアントで容易に十数件まで増える。上から数件見れば
			// 基点の取り違えは判断できるので、ここで打ち切って残件数だけ添える。
			static constexpr size_t MAX_LOGGED_CANDIDATE_COUNT = 8;


			/** 空でなく、まだ含まれていない候補だけを追記する */
			void PushUniquePath(std::vector<std::string>& paths, const std::string& path)
			{
				if (!path.empty() && std::find(paths.begin(), paths.end(), path) == paths.end()) {
					paths.push_back(path);
				}
			}


			/**
			 * 既存の候補それぞれについて、拡張子だけを小文字化/大文字化した候補を末尾へ足す。
			 *
			 * **大文字小文字を区別するファイルシステム対策。** tkm のマテリアルは
			 * 参照テクスチャの拡張子を小文字 ".dds" へ machine 的に置換する
			 * (ReplaceExtension(..., ".dds"))が、同梱アセットの実体は "utc_all2.DDS" の
			 * ように大文字のことがある。Windows / macOS のボリュームは既定で
			 * 大文字小文字を区別しないため今まで表面化しなかったが、
			 * **iOS(シミュレータ・実機とも)と Android の内部ストレージは区別する**ので、
			 * そのままではテクスチャが開けず「モデルだけ出て真っ白/灰色」になる
			 * (iOS 移植 P2 で実際に踏んだ。設計書/iOS移植設計.md)。
			 *
			 * 完全一致を必ず優先するため、**元の候補をすべて並べた後**に足す。
			 * 探索は「存在するものを 1 つ見つけるまで」なので、当たっている環境では
			 * ここまで到達せず追加コストは無い。
			 */
			void PushExtensionCaseVariants(std::vector<std::string>& paths)
			{
				const size_t originalCount = paths.size();
				for (size_t i = 0; i < originalCount; ++i) {
					const std::string& original = paths[i];

					const size_t dot = original.find_last_of('.');
					if (dot == std::string::npos) {
						continue;
					}
					// セパレータより後に '.' が無いものは拡張子ではない("../foo" など)。
					const size_t separator = original.find_last_of('/');
					if (separator != std::string::npos && dot < separator) {
						continue;
					}

					std::string lower = original;
					std::string upper = original;
					for (size_t c = dot + 1; c < original.size(); ++c) {
						lower[c] = static_cast<char>(std::tolower(static_cast<unsigned char>(original[c])));
						upper[c] = static_cast<char>(std::toupper(static_cast<unsigned char>(original[c])));
					}
					PushUniquePath(paths, lower);
					PushUniquePath(paths, upper);
				}
			}
		}


		const std::string& FindContentRoot()
		{
			// 関数ローカル static でプロセス終了まで残る。リーク報告の対象外にする。
			aq::memory::ScopedPersistentAlloc persistent;

			// ワーカースレッドから並列に呼ばれる(設計書/07_リソース管理設計.md)ため、
			// C++11 のスレッドセーフな static 初期化で一度だけ算出する。
			// 「空なら計算する」方式は同時呼び出しで二重初期化になるので採らない。
			static const std::string cached = []() -> std::string
				{
					// プラットフォームがコンテンツ基点を返す場合(UWP のパッケージ install
					// フォルダ、macOS の Contents/Resources、iOS のバンドル、Android の
					// 展開先)は、それを基点に採用し、ソースツリーの上方探索は行わない。
					// サンドボックスでは番兵を遡れないため。Win32 は nullptr を返すので
					// 従来どおり下の探索にフォールバックする。
					if (const char* contentRoot = aq::Engine::Get().GetContentRoot()) {
						return contentRoot;
					}

					std::error_code ec;
					std::filesystem::path dir = std::filesystem::current_path(ec);
					if (ec) {
						return std::string();
					}

					while (!dir.empty()) {
						// 番兵は aqEngine/Assets。ゲーム側の Assets を番兵にすると、
						// アセットを 1 個も持たないゲーム(最小サンプル)が基点を見つけられない。
						// エンジンのアセットは必ず存在する。
						if (std::filesystem::exists(dir / "aqEngine" / "Assets", ec) && !ec) {
							return dir.generic_string();
						}
						if (dir == dir.root_path()) {
							break;
						}
						dir = dir.parent_path();
					}

					return std::filesystem::current_path(ec).generic_string();
				}();
			return cached;
		}


		void BuildAssetPathCandidates(const std::string& path, std::vector<std::string>& candidates)
		{
			candidates.clear();

			std::string normalized = path;
			std::replace(normalized.begin(), normalized.end(), '\\', '/');
			PushUniquePath(candidates, normalized);

			if (std::filesystem::path(normalized).is_absolute()) {
				// 絶対パスでも拡張子の大小だけは面倒を見る。tkm のマテリアルは
				// 解決済みの絶対パスを基点に組み立てられるため、ここを素通りすると
				// 大文字小文字を区別する環境でテクスチャが 1 枚も開けない。
				PushExtensionCaseVariants(candidates);
				return;
			}

			// UWP でもパッケージ内にソースツリー相対構造を再現するため、デスクトップと同じ規則で解決。
			const std::filesystem::path root(FindContentRoot());
			if (normalized.rfind("Assets/", 0) == 0) {
				PushUniquePath(candidates, (root / "Game" / normalized).generic_string());
			} else if (normalized.rfind("Game/Assets/", 0) == 0) {
				PushUniquePath(candidates, (root / normalized).generic_string());
			} else {
				PushUniquePath(candidates, (root / normalized).generic_string());
			}
			PushExtensionCaseVariants(candidates);
		}


		std::string ResolveExistingAssetPath(const std::string& path)
		{
			std::vector<std::string> candidates;
			BuildAssetPathCandidates(path, candidates);

			std::error_code ec;
			for (const std::string& candidate : candidates) {
				if (std::filesystem::exists(candidate, ec) && !ec) {
					return candidate;
				}
			}

			// 正規化した文字列ではなく入力そのままを返す。呼び出し側が
			// 返り値を元のパスと比較している箇所があるため。
			LogUnresolvedAssetPath(path);
			return path;
		}


		std::string ResolveShaderPath(const char* filePath)
		{
			const std::string input = filePath ? filePath : "";

			std::vector<std::string> candidates;
			BuildAssetPathCandidates(input, candidates);

			std::error_code ec;
			for (const std::string& candidate : candidates) {
				if (std::filesystem::exists(candidate, ec) && !ec) {
					return candidate;
				}
			}

			LogUnresolvedAssetPath(input);

			// 見つからなかったときは区切りを / へ正規化した入力を返す。
			// 各シェーダバックエンドの既存フォールバックがこの形を前提にしている。
			std::string normalized = input;
			std::replace(normalized.begin(), normalized.end(), '\\', '/');
			return normalized;
		}


		void LogUnresolvedAssetPath(const std::string& path)
		{
			std::vector<std::string> candidates;
			BuildAssetPathCandidates(path, candidates);

			aq::StartupMarkf("[asset] 見つかりません: %s  (コンテンツ基点: %s)",
				path.c_str(), FindContentRoot().c_str());

			const size_t total = candidates.size();
			const size_t shown = (total < MAX_LOGGED_CANDIDATE_COUNT) ? total : MAX_LOGGED_CANDIDATE_COUNT;
			for (size_t i = 0; i < shown; ++i) {
				aq::StartupMarkf("[asset]   候補 %u/%u: %s",
					static_cast<unsigned int>(i + 1),
					static_cast<unsigned int>(total),
					candidates[i].c_str());
			}
			if (total > shown) {
				aq::StartupMarkf("[asset]   (残り %u 件は省略)",
					static_cast<unsigned int>(total - shown));
			}
		}
	}
}
