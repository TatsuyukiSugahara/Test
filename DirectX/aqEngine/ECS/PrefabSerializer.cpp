#include "aq.h"
#include "PrefabSerializer.h"
#include <set>

namespace aq
{
	namespace ecs
	{
		namespace
		{
			// 参照展開のロードコンテキスト（設計 §7.4）。
			struct LoadContext
			{
				std::vector<std::string>                         stack;       // ロード中の正規化パス（循環検出）
				std::unordered_map<std::string, util::JsonValue> parseCache;  // 正規化パス → パース済み JSON
				int                                              depth = 0;
			};
			constexpr int kMaxRefDepth = 32;

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

			bool ParseFileCached(const std::string& normPath, LoadContext& ctx, util::JsonValue& out)
			{
				auto it = ctx.parseCache.find(normPath);
				if (it != ctx.parseCache.end()) { out = it->second; return true; }
				util::JsonValue parsed = util::JsonParser::ParseFile(normPath.c_str());
				if (parsed.IsNull()) return false;
				ctx.parseCache[normPath] = parsed;
				out = std::move(parsed);
				return true;
			}

			// 前方宣言（相互再帰）
			util::JsonValue ResolveNode(const util::JsonValue& nodeJson, const std::string& baseDir, LoadContext& ctx);

			// components へパッチを適用し、新しい components オブジェクトを返す（設計 §7.3）。
			//   - patch.components       … 既存へ deep merge（無ければ追加）
			//   - patch.addedComponents  … 新規追加
			//   - patch.removedComponents… typeName 配列で除去
			util::JsonValue PatchComponents(const util::JsonValue& baseComps, const util::JsonValue& patch)
			{
				std::set<std::string> removed;
				if (patch.Contains("removedComponents") && patch["removedComponents"].IsArray())
					for (const util::JsonValue& r : patch["removedComponents"].GetArray())
						if (r.IsString()) removed.insert(r.AsString());

				util::JsonValue result = util::JsonValue::MakeObject();
				if (baseComps.IsObject())
					for (const auto& kv : baseComps.GetObject())
						if (!removed.count(kv.first)) result.Set(kv.first, kv.second);

				if (patch.Contains("components") && patch["components"].IsObject())
					for (const auto& kv : patch["components"].GetObject())
					{
						if (removed.count(kv.first)) continue;
						if (result.Contains(kv.first))
						{
							util::JsonValue merged = result[kv.first];
							merged.Merge(kv.second);          // 既存へ deep merge
							result.Set(kv.first, std::move(merged));
						}
						else result.Set(kv.first, kv.second);
					}

				if (patch.Contains("addedComponents") && patch["addedComponents"].IsObject())
					for (const auto& kv : patch["addedComponents"].GetObject())
						if (!removed.count(kv.first)) result.Set(kv.first, kv.second);

				return result;
			}

			// 解決済み canonical ノード base へパッチ（overrides / override-child エントリ）を適用する。
			// children は name 同定: 同名は再帰適用、無ければ新規ノードとして解決して追加、removedChildren で除去。
			util::JsonValue ApplyPatch(const util::JsonValue& base, const util::JsonValue& patch,
			                           const std::string& baseDir, LoadContext& ctx)
			{
				util::JsonValue result = util::JsonValue::MakeObject();

				// name（patch が上書きする場合のみ）
				std::string name = base.Contains("name") ? base["name"].AsString() : std::string();
				if (patch.Contains("name")) name = patch["name"].AsString();
				result.Set("name", util::JsonValue(name));

				// components
				const util::JsonValue baseComps =
					base.Contains("components") ? base["components"] : util::JsonValue::MakeObject();
				result.Set("components", PatchComponents(baseComps, patch));

				// children（name 同定でマージ / 追加 / 除去）
				std::set<std::string> removedChildren;
				if (patch.Contains("removedChildren") && patch["removedChildren"].IsArray())
					for (const util::JsonValue& r : patch["removedChildren"].GetArray())
						if (r.IsString()) removedChildren.insert(r.AsString());

				const util::JsonValue& ovChildren = patch["children"];   // 無ければ Null（IsArray()=false）
				util::JsonValue outChildren = util::JsonValue::MakeArray();

				const bool baseHasChildren = base.Contains("children") && base["children"].IsArray();

				// 既存の子を出力（除去対象を除く / 同名 override があれば再帰適用）
				if (baseHasChildren)
					for (const util::JsonValue& child : base["children"].GetArray())
					{
						const std::string cname = child.Contains("name") ? child["name"].AsString() : std::string();
						if (removedChildren.count(cname)) continue;

						const util::JsonValue* matched = nullptr;
						if (ovChildren.IsArray())
							for (const util::JsonValue& oc : ovChildren.GetArray())
								if (oc.Contains("name") && oc["name"].AsString() == cname) { matched = &oc; break; }

						outChildren.PushBack(matched ? ApplyPatch(child, *matched, baseDir, ctx) : child);
					}

				// override.children のうち既存に無いものは新規ノードとして解決・追加
				if (ovChildren.IsArray())
					for (const util::JsonValue& oc : ovChildren.GetArray())
					{
						const std::string ocname = oc.Contains("name") ? oc["name"].AsString() : std::string();
						if (removedChildren.count(ocname)) continue;

						bool existed = false;
						if (baseHasChildren)
							for (const util::JsonValue& child : base["children"].GetArray())
							{
								const std::string cname = child.Contains("name") ? child["name"].AsString() : std::string();
								if (cname == ocname) { existed = true; break; }
							}
						if (!existed) outChildren.PushBack(ResolveNode(oc, baseDir, ctx));
					}

				result.Set("children", std::move(outChildren));
				return result;
			}

			// ノード JSON を解決して canonical な { name, components, children } を返す（設計 §7）。
			// "prefab" があれば参照先を再帰展開（循環検出・最大深度）し、name/overrides を適用する。
			util::JsonValue ResolveNode(const util::JsonValue& nodeJson, const std::string& baseDir, LoadContext& ctx)
			{
				if (ctx.depth >= kMaxRefDepth)
				{
					EnginePrintf("[Prefab] max reference depth (%d) exceeded\n", kMaxRefDepth);
					return util::JsonValue::MakeObject();
				}

				util::JsonValue base;

				if (nodeJson.Contains("prefab"))
				{
					const std::string refPath = JoinPath(baseDir, nodeJson["prefab"].AsString());

					for (const std::string& s : ctx.stack)
						if (s == refPath)
						{
							EnginePrintf("[Prefab] circular reference detected: %s\n", refPath.c_str());
							return util::JsonValue::MakeObject();
						}

					util::JsonValue refRoot;
					if (!ParseFileCached(refPath, ctx, refRoot))
					{
						EnginePrintf("[Prefab] failed to load referenced prefab: %s\n", refPath.c_str());
						return util::JsonValue::MakeObject();
					}

					ctx.stack.push_back(refPath);
					++ctx.depth;
					base = ResolveNode(refRoot, DirOf(refPath), ctx);
					--ctx.depth;
					ctx.stack.pop_back();
				}
				else
				{
					// インライン: canonical コピー + children 再帰解決
					base = util::JsonValue::MakeObject();
					base.Set("components",
						nodeJson.Contains("components") ? nodeJson["components"] : util::JsonValue::MakeObject());

					util::JsonValue childArr = util::JsonValue::MakeArray();
					if (nodeJson.Contains("children") && nodeJson["children"].IsArray())
						for (const util::JsonValue& child : nodeJson["children"].GetArray())
							childArr.PushBack(ResolveNode(child, baseDir, ctx));
					base.Set("children", std::move(childArr));
				}

				// name は参照ルート名より nodeJson 側を優先（インラインでも設定）
				if (nodeJson.Contains("name")) base.Set("name", nodeJson["name"]);

				// overrides 適用（新規子は呼び出し元 baseDir 基準で解決）
				if (nodeJson.Contains("overrides"))
					base = ApplyPatch(base, nodeJson["overrides"], baseDir, ctx);

				return base;
			}


			// 解決済み canonical JSON → PrefabNodeData（参照は残っていない）。
			PrefabNodeData BuildNode(const util::JsonValue& json)
			{
				PrefabNodeData node;
				if (json.Contains("name"))       node.name       = json["name"].AsString();
				if (json.Contains("components") && json["components"].IsObject())
					node.components = json["components"];
				if (json.Contains("children") && json["children"].IsArray())
					for (const util::JsonValue& child : json["children"].GetArray())
						node.children.push_back(BuildNode(child));
				return node;
			}

			Prefab BuildPrefab(const util::JsonValue& root, const std::string& baseDir, LoadContext& ctx)
			{
				const util::JsonValue resolved = ResolveNode(root, baseDir, ctx);
				auto data  = std::make_shared<PrefabData>();
				data->root = BuildNode(resolved);
				return Prefab(std::shared_ptr<const PrefabData>(std::move(data)));
			}
		}


		Prefab PrefabSerializer::FromJson(const util::JsonValue& root, const char* baseDir)
		{
			if (!root.IsObject())
			{
				EngineAssertMsg(false, "PrefabSerializer::FromJson: root is not a JSON object");
				return Prefab();
			}
			LoadContext ctx;
			return BuildPrefab(root, baseDir ? baseDir : "", ctx);
		}


		Prefab PrefabSerializer::Load(const char* path)
		{
			const std::string norm = NormalizePath(path);
			util::JsonValue root = util::JsonParser::ParseFile(norm.c_str());
			if (root.IsNull())
			{
				EngineAssertMsg(false, "PrefabSerializer::Load: failed to parse prefab JSON file");
				return Prefab();
			}

			LoadContext ctx;
			ctx.stack.push_back(norm);          // ルート自身も循環検出スタックへ（子からの自己参照を検出）
			ctx.parseCache[norm] = root;
			return BuildPrefab(root, DirOf(norm), ctx);
		}
	}
}
