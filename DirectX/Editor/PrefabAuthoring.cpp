#include "aq.h"
#include "PrefabAuthoring.h"
#include "ECS/PrefabEditNodeOps.h"
#include "Level/LevelEditModel.h"
#include <algorithm>
#include <filesystem>
#include <system_error>


namespace editor
{
	namespace
	{
		namespace fs = std::filesystem;


		/** '\' を '/' にする */
		std::string ToSlash(const std::string& path)
		{
			std::string result = path;
			std::replace(result.begin(), result.end(), '\\', '/');
			return result;
		}


		/** 字句だけで正規化して '/' 区切りにする(ファイルシステムは見ない) */
		std::string Normalize(const std::string& path)
		{
			return fs::path(ToSlash(path)).lexically_normal().generic_string();
		}


		/** 作業ディレクトリを基準に絶対パスにする(正規化済み、'/' 区切り)。取れなければそのまま正規化だけ */
		std::string ToAbsolute(const std::string& path)
		{
			if (PrefabAuthoring::IsAbsoluteReference(path)) return Normalize(path);

			std::error_code ec;
			const fs::path  current = fs::current_path(ec);
			if (ec) return Normalize(path);
			return (current / fs::path(ToSlash(path))).lexically_normal().generic_string();
		}


		/** ノードの JSON 1 つの参照を書き換えた複製を返す(prefab / children / overrides.children を再帰でたどる) */
		aq::util::JsonValue RebaseNode(const aq::util::JsonValue& node, const std::string& fromDir, const std::string& toDir)
		{
			using aq::util::JsonValue;
			if (!node.IsObject()) return node;

			JsonValue result = JsonValue::MakeObject();
			for (const auto& entry : node.GetObject()) {
				const std::string& key   = entry.first;
				const JsonValue&   value = entry.second;

				if (key == "prefab" && value.IsString()) {
					result.Set(key, JsonValue(PrefabAuthoring::RebaseReference(value.AsString(), fromDir, toDir)));
				}
				else if (key == "children" && value.IsArray()) {
					JsonValue children = JsonValue::MakeArray();
					for (const JsonValue& child : value.GetArray()) children.PushBack(RebaseNode(child, fromDir, toDir));
					result.Set(key, std::move(children));
				}
				else if (key == "overrides" && value.IsObject()) {
					// override の children だけが参照を持つ(components などの値は変えない)
					JsonValue overrides = JsonValue::MakeObject();
					for (const auto& overrideEntry : value.GetObject()) {
						if (overrideEntry.first == "children" && overrideEntry.second.IsArray()) {
							JsonValue children = JsonValue::MakeArray();
							for (const JsonValue& child : overrideEntry.second.GetArray()) children.PushBack(RebaseNode(child, fromDir, toDir));
							overrides.Set(overrideEntry.first, std::move(children));
						}
						else {
							overrides.Set(overrideEntry.first, overrideEntry.second);
						}
					}
					result.Set(key, std::move(overrides));
				}
				else {
					result.Set(key, value);
				}
			}
			return result;
		}


		/** error があれば理由を入れて false を返す */
		bool Fail(std::string* error, const char* reason)
		{
			if (error != nullptr) *error = reason;
			return false;
		}
	}




	std::string PrefabAuthoring::SanitizeFileName(const std::string& nodeName)
	{
		// 前後の空白を落とす
		const char* spaces = " \t\n\r\v\f";
		const size_t first = nodeName.find_first_not_of(spaces);
		if (first == std::string::npos) return "Node";
		const size_t last = nodeName.find_last_not_of(spaces);

		std::string result = nodeName.substr(first, last - first + 1);
		for (char& c : result) {
			const unsigned char u = static_cast<unsigned char>(c);
			if (u < 0x20 || u == 0x7F || std::string("\\/:*?\"<>|").find(c) != std::string::npos) c = '_';
		}
		return result;
	}


	std::string PrefabAuthoring::FindFreePrefabPath(const std::string& folder, const std::string& nodeName)
	{
		std::string directory = ToSlash(folder);
		while (directory.size() > 1 && directory.back() == '/') directory.pop_back();
		const std::string prefix = directory.empty() ? std::string() : (directory == "/" ? directory : directory + "/");

		const std::string baseName = SanitizeFileName(nodeName);
		std::string       path     = prefix + baseName + PREFAB_SUFFIX;
		for (int number = 1; number < 100000; ++number) {
			std::error_code ec;
			// 調べられない(エラー)ときは無い扱いにせず、次の名前へ進む
			if (!fs::exists(fs::path(path), ec) && !ec) break;
			path = prefix + baseName + "_" + std::to_string(number) + PREFAB_SUFFIX;
		}
		return path;
	}


	std::string PrefabAuthoring::ReferenceNameFromPath(const std::string& prefabPath)
	{
		const std::string slashed = ToSlash(prefabPath);
		const size_t      slash   = slashed.find_last_of('/');
		std::string       name    = (slash == std::string::npos) ? slashed : slashed.substr(slash + 1);

		// 大文字小文字を無視して末尾の .prefab.json を除く
		const std::string suffix = PREFAB_SUFFIX;
		if (name.size() >= suffix.size()) {
			std::string tail = name.substr(name.size() - suffix.size());
			for (char& c : tail) {
				if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
			}
			if (tail == suffix) name.resize(name.size() - suffix.size());
		}
		return name;
	}


	bool PrefabAuthoring::IsAbsoluteReference(const std::string& ref)
	{
		const std::string r = ToSlash(ref);
		return (!r.empty() && r[0] == '/') || (r.size() >= 2 && r[1] == ':');
	}


	std::string PrefabAuthoring::MakeReferencePath(const std::string& baseDir, const std::string& assetPath)
	{
		if (baseDir.empty()) return assetPath;

		const bool baseAbsolute  = IsAbsoluteReference(baseDir);
		const bool assetAbsolute = IsAbsoluteReference(assetPath);

		// 片方だけ絶対なら、相対の方を作業ディレクトリ基準の絶対パスにそろえる
		std::string base  = Normalize(baseDir);
		std::string asset = Normalize(assetPath);
		if (baseAbsolute != assetAbsolute) {
			if (baseAbsolute) asset = ToAbsolute(assetPath);
			else              base  = ToAbsolute(baseDir);
		}

		const std::string relative = fs::path(asset).lexically_relative(fs::path(base)).generic_string();
		if (!relative.empty()) return relative;

		// 相対にできない(ドライブが違うなど)ときは絶対パス
		return ToAbsolute(assetPath);
	}


	std::string PrefabAuthoring::RebaseReference(const std::string& ref, const std::string& fromDir, const std::string& toDir)
	{
		if (ref.empty() || IsAbsoluteReference(ref)) return ref;

		// fromDir が空なら ref は作業ディレクトリ基準
		const std::string joined = fromDir.empty() ? Normalize(ref) : Normalize(fromDir + "/" + ref);
		return MakeReferencePath(toDir, joined);
	}


	void PrefabAuthoring::RebaseReferencesInNodeJson(aq::util::JsonValue& nodeJson, const std::string& fromDir, const std::string& toDir)
	{
		nodeJson = RebaseNode(nodeJson, fromDir, toDir);
	}


	bool PrefabAuthoring::CreatePrefabFromNode(const aq::level::LevelEditModel& model, const uint32_t nodeId, const std::string& folder,
	                                           std::string& outPath, std::string* error)
	{
		const aq::ecs::PrefabEditNode* node = model.FindNode(nodeId);
		if (node == nullptr)          return Fail(error, "The node does not exist.");
		if (!node->prefabRef.empty()) return Fail(error, "A prefab reference node cannot be made into a prefab.");
		if (node->editId == 0)        return Fail(error, "The node has no edit id.");

		// 参照は Level の基準ディレクトリ基準から、新しい Prefab のあるフォルダ基準へ書き換える
		aq::util::JsonValue json = aq::ecs::PrefabNodeToJson(*node);
		RebaseReferencesInNodeJson(json, model.GetBaseDir(), folder);

		// 既存のファイルは上書きしない
		const std::string path = FindFreePrefabPath(folder, node->name);
		if (!aq::util::JsonSerializer::WriteFile(path.c_str(), json)) return Fail(error, "Failed to write the prefab file.");

		outPath = path;
		const std::string message = "[editor] prefab created: " + path;
		aq::StartupLog(message.c_str());
		return true;
	}
}
