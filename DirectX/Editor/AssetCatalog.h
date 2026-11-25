#pragma once
#include "ECS/FieldAttr.h"
#include <string>
#include <vector>


namespace editor
{
	/**
	 * アセットの一覧の 1 ファイル
	 */
	struct AssetEntry
	{
		std::string        name;                               // ファイル名(拡張子を含む)
		std::string        path;                               // 根からのパス(Assets/... の '/' 区切り)
		aq::ecs::AssetKind kind = aq::ecs::AssetKind::None;   // ClassifyAsset の結果
	};




	/**
	 * アセットの一覧の 1 フォルダ。並びはフォルダ・ファイルとも名前の大文字小文字を無視した順
	 */
	struct AssetFolder
	{
		std::string              name;      // フォルダ名(根は根のパスそのもの)
		std::string              path;      // 根からのパス(根は "Assets")
		std::vector<AssetFolder> folders;
		std::vector<AssetEntry>  files;
	};




	/**
	 * アセットのフォルダの木(エディタ全体設計 7.10 節 1.)。
	 * 根のフォルダを std::filesystem でフォルダごとに読み、木にして持つ。読み直すのは Refresh を呼んだときだけ。
	 * 読めないフォルダは飛ばしてログに 1 行出す。シンボリックリンクはたどらず、名前が '.' で始まるものは出さない。
	 * ImGui に依らない
	 */
	class AssetCatalog
	{
	public:
		/** 既定の根(作業ディレクトリからの相対。コンポーネントのパスの書き方 Assets/... に合わせる) */
		static constexpr const char* DEFAULT_ROOT = "Assets";


	private:
		/** 根のパス('/' 区切り) */
		std::string root_ = DEFAULT_ROOT;

		/** 読んだ木 */
		AssetFolder tree_;

		/** 一度でも読んだか */
		bool isLoaded_ = false;


	public:
		AssetCatalog() = default;
		explicit AssetCatalog(const std::string& root);

		/** 根から読み直す。根が無い・フォルダでないときは空の木にして false */
		bool Refresh();

		/** path のフォルダ(無ければ nullptr) */
		const AssetFolder* FindFolder(const std::string& path) const;

		inline const AssetFolder& GetTree() const  { return tree_; }
		inline const std::string& GetRoot() const  { return root_; }
		inline bool               IsLoaded() const { return isLoaded_; }


	public:
		/**
		 * パスの拡張子からアセットの種類を決める(大文字小文字を無視する)。
		 * .prefab.json → Prefab、.level.json → Level、.fbx .tkm → Model、.png .dds .jpg .jpeg .tga → Texture、
		 * .particle → Particle、.wav .ogg → Audio、それ以外 → None
		 */
		static aq::ecs::AssetKind ClassifyAsset(const std::string& path);

		/** 種類の短い印(一覧の先頭に出す。[M] [T] [P] [L] [FX] [A]、None は空文字列) */
		static const char* GetKindTag(const aq::ecs::AssetKind kind);
	};
}
