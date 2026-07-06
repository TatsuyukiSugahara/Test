#include "aq.h"
#include "Level/LevelSerializer.h"
#include "ECS/PrefabSerializer.h"


namespace aq
{
	namespace level
	{
		namespace
		{
			std::string NormalizePath(std::string s)
			{
				for (char& c : s) if (c == '\\') c = '/';
				return s;
			}

			std::string DirOf(const std::string& path)
			{
				const size_t pos = path.find_last_of('/');
				return (pos == std::string::npos) ? std::string() : path.substr(0, pos);
			}

			// dir 基準で rel を結合・正規化する。rel が絶対（先頭 '/' or 'X:'）なら rel をそのまま使う。
			std::string JoinPath(const std::string& dir, const std::string& rel)
			{
				const std::string r = NormalizePath(rel);
				const bool absolute = (!r.empty() && r[0] == '/') || (r.size() >= 2 && r[1] == ':');
				if (absolute || dir.empty()) return r;
				return NormalizePath(dir + "/" + r);
			}

			// パース済み Level ルート JSON → LevelData。
			// entities の各要素は ecs::PrefabSerializer::FromJson で 1 ツリーに解決する
			// （"prefab" 参照・overrides・循環検出をそのまま再利用）。
			std::shared_ptr<const LevelData> Build(const util::JsonValue& root, const std::string& baseDir)
			{
				if (!root.IsObject())
				{
					EngineAssertMsg(false, "LevelSerializer: root is not a JSON object");
					return nullptr;
				}

				auto data = std::make_shared<LevelData>();

				if (root.Contains("name")) data->name = root["name"].AsString();

				// entities: Prefab ノードのフォレスト
				if (root.Contains("entities") && root["entities"].IsArray())
				{
					for (const util::JsonValue& node : root["entities"].GetArray())
					{
						ecs::Prefab prefab = ecs::PrefabSerializer::FromJson(node, baseDir.c_str());
						if (prefab.IsValid()) data->entities.push_back(prefab.Data()->root);
					}
				}

				// subLevels: 別 .level.json への参照（L4 まで内容は保持のみ・ロードはしない）
				if (root.Contains("subLevels") && root["subLevels"].IsArray())
				{
					for (const util::JsonValue& sub : root["subLevels"].GetArray())
					{
						if (!sub.IsObject() || !sub.Contains("level")) continue;
						SubLevelRef ref;
						ref.loadOnStart = sub.Contains("loadOnStart") ? sub["loadOnStart"].AsBool(true) : true;

						const util::JsonValue& lv = sub["level"];
						if (lv.IsObject()) {
							// インライン定義: その場で LevelData を構築する（entities/subLevels を再帰解決）。
							ref.inlineData = Build(lv, baseDir);
						} else if (lv.IsString()) {
							// 外部ファイル参照: 親 .level.json のディレクトリ基準で解決しておく（再帰ロードで直接使える）。
							ref.path = JoinPath(baseDir, lv.AsString());
						} else {
							continue;
						}
						data->subLevels.push_back(std::move(ref));
					}
				}

				return data;
			}
		}


		std::shared_ptr<const LevelData> LevelSerializer::FromJson(const util::JsonValue& root, const char* baseDir)
		{
			return Build(root, baseDir ? baseDir : "");
		}


		std::shared_ptr<const LevelData> LevelSerializer::Load(const char* path)
		{
			const std::string norm = NormalizePath(path);
			util::JsonValue root = util::JsonParser::ParseFile(norm.c_str());
			if (root.IsNull())
			{
				EngineAssertMsg(false, "LevelSerializer::Load: failed to parse level JSON file");
				return nullptr;
			}
			return Build(root, DirOf(norm));
		}
	}
}
