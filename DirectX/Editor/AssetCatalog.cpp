#include "aq.h"
#include "AssetCatalog.h"
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>


namespace editor
{
	namespace
	{
		/** フォルダを深く読みすぎないための上限(循環はシンボリックリンクをたどらないので起きないが念のため) */
		static constexpr int MAX_FOLDER_DEPTH = 64;


		/** ASCII の英字を小文字にする(拡張子と並びの比較用。日本語などはそのまま) */
		std::string ToLowerAscii(const std::string& text)
		{
			std::string result = text;
			for (char& c : result) {
				if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
			}
			return result;
		}


		/** text が suffix で終わるか(呼び出し側で小文字にしてから渡す) */
		bool EndsWith(const std::string& text, const char* suffix)
		{
			const size_t length = std::strlen(suffix);
			return text.size() >= length && text.compare(text.size() - length, length, suffix) == 0;
		}


		/** 名前の大文字小文字を無視した順。同じなら元の名前の順 */
		bool NameLess(const std::string& a, const std::string& b)
		{
			const std::string lowerA = ToLowerAscii(a);
			const std::string lowerB = ToLowerAscii(b);
			if (lowerA != lowerB) return lowerA < lowerB;
			return a < b;
		}


		/** 親のパスと名前を '/' でつなぐ */
		std::string JoinPath(const std::string& parent, const std::string& name)
		{
			if (parent.empty()) return name;
			if (parent.back() == '/') return parent + name;
			return parent + "/" + name;
		}


		/** 末尾の '/' と '\' を落とし、'\' を '/' にする(根だけ "/" のときは残す) */
		std::string NormalizeRoot(const std::string& root)
		{
			std::string result = root;
			std::replace(result.begin(), result.end(), '\\', '/');
			while (result.size() > 1 && result.back() == '/') result.pop_back();
			return result;
		}


		/** ログに 1 行出す */
		void LogReadFailure(const std::string& path, const std::error_code& error)
		{
			const std::string message = "[editor] asset folder skipped: " + path + " (" + error.message() + ")";
			aq::StartupLog(message.c_str());
		}


		/** folder.path のフォルダを読み、folder の中身を作る(再帰はフォルダごと) */
		void ReadFolder(AssetFolder& folder, const int depth)
		{
			if (depth >= MAX_FOLDER_DEPTH) return;

			std::error_code error;
			std::filesystem::directory_iterator       it(std::filesystem::path(folder.path), error);
			const std::filesystem::directory_iterator end;
			if (error) {
				LogReadFailure(folder.path, error);
				return;
			}

			for (; it != end; it.increment(error)) {
				if (error) {
					LogReadFailure(folder.path, error);
					break;
				}

				const std::string name = it->path().filename().string();
				if (name.empty() || name[0] == '.') continue;

				// symlink_status はリンクそのものを見る(リンクはたどらない)
				std::error_code statusError;
				const std::filesystem::file_status status = it->symlink_status(statusError);
				if (statusError) continue;

				if (std::filesystem::is_directory(status)) {
					AssetFolder child;
					child.name = name;
					child.path = JoinPath(folder.path, name);
					folder.folders.push_back(std::move(child));
				} else if (std::filesystem::is_regular_file(status)) {
					AssetEntry entry;
					entry.name = name;
					entry.path = JoinPath(folder.path, name);
					entry.kind = AssetCatalog::ClassifyAsset(entry.path);
					folder.files.push_back(std::move(entry));
				}
			}

			std::sort(folder.folders.begin(), folder.folders.end(),
				[](const AssetFolder& a, const AssetFolder& b) { return NameLess(a.name, b.name); });
			std::sort(folder.files.begin(), folder.files.end(),
				[](const AssetEntry& a, const AssetEntry& b) { return NameLess(a.name, b.name); });

			for (AssetFolder& child : folder.folders) ReadFolder(child, depth + 1);
		}


		/** folder の下から path のフォルダを探す */
		const AssetFolder* FindFolderIn(const AssetFolder& folder, const std::string& path)
		{
			if (folder.path == path) return &folder;
			// path が folder.path + "/" で始まるときだけ下へ降りる
			if (path.size() <= folder.path.size() || path.compare(0, folder.path.size(), folder.path) != 0) return nullptr;
			if (path[folder.path.size()] != '/' && folder.path.back() != '/') return nullptr;

			for (const AssetFolder& child : folder.folders) {
				const AssetFolder* found = FindFolderIn(child, path);
				if (found != nullptr) return found;
			}
			return nullptr;
		}
	}




	AssetCatalog::AssetCatalog(const std::string& root)
		: root_(NormalizeRoot(root))
	{
	}


	bool AssetCatalog::Refresh()
	{
		tree_ = AssetFolder();
		tree_.name = root_;
		tree_.path = root_;
		isLoaded_ = true;

		std::error_code error;
		if (!std::filesystem::is_directory(std::filesystem::path(root_), error) || error) return false;

		ReadFolder(tree_, 0);
		return true;
	}


	const AssetFolder* AssetCatalog::FindFolder(const std::string& path) const
	{
		return FindFolderIn(tree_, path);
	}


	aq::ecs::AssetKind AssetCatalog::ClassifyAsset(const std::string& path)
	{
		const std::string lower = ToLowerAscii(path);

		// 二重拡張子を先に見る(.json だけでは種類にならない)
		if (EndsWith(lower, ".prefab.json")) return aq::ecs::AssetKind::Prefab;
		if (EndsWith(lower, ".level.json"))  return aq::ecs::AssetKind::Level;

		if (EndsWith(lower, ".fbx") || EndsWith(lower, ".tkm")) return aq::ecs::AssetKind::Model;
		if (EndsWith(lower, ".png") || EndsWith(lower, ".dds") || EndsWith(lower, ".jpg")
			|| EndsWith(lower, ".jpeg") || EndsWith(lower, ".tga")) return aq::ecs::AssetKind::Texture;
		if (EndsWith(lower, ".particle")) return aq::ecs::AssetKind::Particle;
		if (EndsWith(lower, ".wav") || EndsWith(lower, ".ogg")) return aq::ecs::AssetKind::Audio;

		return aq::ecs::AssetKind::None;
	}


	const char* AssetCatalog::GetKindTag(const aq::ecs::AssetKind kind)
	{
		switch (kind) {
		case aq::ecs::AssetKind::Model:    return "[M]";
		case aq::ecs::AssetKind::Texture:  return "[T]";
		case aq::ecs::AssetKind::Prefab:   return "[P]";
		case aq::ecs::AssetKind::Level:    return "[L]";
		case aq::ecs::AssetKind::Particle: return "[FX]";
		case aq::ecs::AssetKind::Audio:    return "[A]";
		default:                           return "";
		}
	}
}
