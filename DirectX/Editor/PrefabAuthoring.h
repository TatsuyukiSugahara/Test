#pragma once
#include "Util/SimpleJson.h"
#include <cstdint>
#include <string>


namespace aq
{
	namespace ecs
	{
		struct PrefabEditNode;
	}

	namespace level
	{
		class LevelEditModel;
	}
}


namespace editor
{
	/**
	 * ヒエラルキーのノードから Prefab を作る処理と、Prefab の参照パスの計算(エディタ全体設計 7.10 節 6. / 7.)。
	 * ImGui に依らない。パスはすべて '/' 区切りで返す
	 */
	class PrefabAuthoring final
	{
	public:
		/** Prefab のファイルの末尾 */
		static constexpr const char* PREFAB_SUFFIX = ".prefab.json";


	public:
		/**
		 * ノード名をファイル名に使える形にする(拡張子は付けない)。
		 * \ / : * ? " < > | と制御文字を '_' にし、前後の空白を落とす。空なら "Node"
		 */
		static std::string SanitizeFileName(const std::string& nodeName);

		/**
		 * folder の中で、まだ無い Prefab のパスを決める。
		 * "<folder>/<SanitizeFileName(nodeName)>.prefab.json" が無ければそれ、あれば "_1" から順に空いている名前。
		 * 既存のファイルは上書きしない
		 */
		static std::string FindFreePrefabPath(const std::string& folder, const std::string& nodeName);

		/** Prefab のパスから、参照ノードの名前にするもの(ファイル名から .prefab.json を除いたもの。大文字小文字を無視して除く) */
		static std::string ReferenceNameFromPath(const std::string& prefabPath);

		/**
		 * 参照の文字列が絶対パスか(PrefabSerializer の JoinPath と同じ判定。'\' を '/' にして、先頭が '/' か 2 文字目が ':')
		 */
		static bool IsAbsoluteReference(const std::string& ref);

		/**
		 * assetPath(作業ディレクトリからのパス、または絶対パス)を、baseDir から見た参照の文字列にする。
		 * baseDir が空(新規の Level = 作業ディレクトリ基準)なら assetPath をそのまま返す。
		 * 相対にできない(ドライブが違うなど)ときは assetPath の絶対パスを返す
		 */
		static std::string MakeReferencePath(const std::string& baseDir, const std::string& assetPath);

		/**
		 * fromDir 基準の参照 ref を、toDir 基準の参照に書き換える。絶対パスはそのまま返す
		 */
		static std::string RebaseReference(const std::string& ref, const std::string& fromDir, const std::string& toDir);

		/**
		 * ノードの JSON(PrefabNodeToJson の形)の中の参照を、fromDir 基準から toDir 基準へ書き換える。
		 * たどるのはノードの "prefab"、"children"、参照ノードの "overrides" の "children"(再帰)。
		 * コンポーネントの中の値は変えない
		 */
		static void RebaseReferencesInNodeJson(aq::util::JsonValue& nodeJson, const std::string& fromDir, const std::string& toDir);

		/**
		 * model の nodeId のノードから Prefab を作り、folder へ書く。
		 * 中身は PrefabNodeToJson(node) の参照を model の基準ディレクトリから folder 基準へ書き換えたもの。
		 * ファイル名は FindFreePrefabPath。書き込みは JsonSerializer::WriteFile(先頭のコメントは付けない)。
		 * ノードが無い・参照ノードそのもの・editId が 0・書き込みに失敗したときは false で、error に理由(英語。UI に出す)。
		 * 成功したら outPath に書いたファイルのパス('/' 区切り)。モデルは変えない
		 */
		static bool CreatePrefabFromNode(const aq::level::LevelEditModel& model, const uint32_t nodeId, const std::string& folder,
		                                 std::string& outPath, std::string* error = nullptr);
	};
}
