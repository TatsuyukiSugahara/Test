#pragma once
// 本ヘッダは意図的に aq.h へ依存しない(Metal の .mm から include するため)。
// 非 Windows の DirectXTex はスタブ basetsd.h 経由で BOOL を再定義し、Cocoa の
// typedef bool BOOL と衝突して @interface が全滅する(設計書/Mac移植設計.md §6)。
// 依存を <string> / <vector> に留めることで、その事故を構造的に防ぐ。
#include <string>
#include <vector>


namespace aq
{
	namespace res
	{
		/**
		 * アセットパス解決の単一実装
		 *
		 * 以前は FindProjectRoot と候補組み立てが Resource.cpp / D3D11Shader.cpp /
		 * D3D12Shader.cpp / VulkanShader.cpp / MetalShader.mm /
		 * MetalRenderContextImpl.mm に計 12 個のコピーとして散っていた。
		 * 挙動の差(GetContentRoot を見るか、拡張子の大小を面倒見るか、
		 * static 初期化がスレッドセーフか)が原因の不具合が実際に出ているため、
		 * ここ 1 箇所に集約する。設計は 設計書/使いやすさ改善設計.md §1.2 が一次資料。
		 */

		/**
		 * コンテンツ基点を求める(プロセスで 1 度だけ算出してキャッシュ)
		 *
		 * Engine::GetContentRoot() を最優先で見る。返れば上方探索は行わない
		 * (サンドボックスでは番兵を遡れないため。UWP のパッケージ install
		 * フォルダ、macOS の Contents/Resources、iOS のバンドル、Android の展開先)。
		 * Win32 は nullptr を返すので、カレントディレクトリから親へ番兵
		 * (`aqEngine/Assets`)を探す。
		 *
		 * ワーカースレッドから並列に呼ばれる([07_リソース管理設計.md] §5)ため、
		 * C++11 のスレッドセーフな static 初期化で算出する。
		 *
		 * @return 末尾セパレータなしの generic 形式パス。求められなければ空文字列
		 */
		const std::string& FindContentRoot();


		/**
		 * `"Assets/..."` を組み立てるときのゲームルート名を設定する(既定 `"Game"`)
		 *
		 * `"Assets/foo.png"` は `<コンテンツ基点>/<ゲームルート名>/Assets/foo.png` へ解決される。
		 * **自分のプロジェクトのフォルダ名を渡すと、`"Assets/..."` が常に自分の
		 * アセットを指すようになる。** フォルダ名を毎回パスへ書く必要がない
		 * (設計書/使いやすさ改善設計.md P3-A)。
		 *
		 * 通常はゲームが `InitializeParameter::gameRootName` に入れ、`Engine::Initialize` が
		 * 呼ぶ。**ワーカースレッドが動き出す前に 1 度だけ**呼ぶこと。解決は複数スレッドから
		 * 並列に走るので、走り出した後に変えるとデータ競合になる。
		 *
		 * @param name ゲームルートのフォルダ名。nullptr / 空文字なら既定の `"Game"` に戻す
		 */
		void SetGameRootName(const char* name);


		/**
		 * 1 つの入力パスに対する探索候補をすべて組む
		 *
		 * 順序は「入力そのまま → コンテンツ基点からの解決 → 拡張子の大小バリアント」。
		 * 完全一致を必ず優先するため、大小バリアントは最後に足す。
		 *
		 * @param path       アセットパス(区切りは / と \ のどちらでもよい)
		 * @param candidates 候補の追記先。呼び出し前に clear される
		 */
		void BuildAssetPathCandidates(const std::string& path, std::vector<std::string>& candidates);


		/**
		 * 実在する最初の候補を返す
		 * @return 見つかればそのパス。見つからなければ入力をそのまま返し、候補一覧をログへ出す
		 */
		std::string ResolveExistingAssetPath(const std::string& path);


		/**
		 * シェーダ用のパス解決(5 バックエンド共通)
		 *
		 * ResolveExistingAssetPath と同じ規則だが、見つからなかったときの戻り値が
		 * 「区切りを / へ正規化した入力」になる点だけ違う(各バックエンドの
		 * 既存フォールバックに合わせている)。
		 */
		std::string ResolveShaderPath(const char* filePath);


		/**
		 * 解決できなかったパスの候補一覧をログへ出す
		 *
		 * 候補を自前で回す呼び出し側(Resource.cpp の OpenBinaryReadWithFallback や
		 * 各シェーダバックエンドの ReadFile)が、全滅したときに呼ぶ。
		 * 「アセットが見つからない」を静かに失敗させないための経路
		 * (設計書/使いやすさ課題.md §1-6)。
		 */
		void LogUnresolvedAssetPath(const std::string& path);
	}
}
