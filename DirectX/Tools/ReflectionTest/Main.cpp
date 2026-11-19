// リフレクション(Reflect と型消去の serializePtr / deserializePtr)の検査ツール
// (設計書 エディタ全体設計.md 6.4 節「5. 検査ツール」。E6a)。
//
//   ReflectionTest --selftest [--assets <Game/Assets のパス>] [--dump <出力ファイル>]
//
// T1 往復 / T2 疎な JSON / T3 既存アセットのダンプ を行う。画面もデバイスも作らない。
// 実体は AlignedStorage(TypeInfo) で既定値のまま作り、ComponentMeta の型消去関数だけを通す。
#include "aq.h"
#include "ECS/ComponentRegistry.h"
#include "ECS/AlignedStorage.h"
#include "Level/LevelComponentRegistry.h"
#include "Util/SimpleJson.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>
#include <algorithm>


namespace
{
	using aq::ecs::AlignedStorage;
	using aq::ecs::ComponentMeta;
	using aq::ecs::ComponentRegistry;
	using aq::ecs::TypeInfo;
	using aq::util::JsonValue;

	/**
	 * 既知の失敗(E6a 時点のエンジンの不具合。設計書 6.4 節の例外 1・例外 3)
	 *
	 * ここに名前がある試験は、失敗しても [XFAIL] として終了コードに数えない。
	 * 通ったら [XPASS] を出して終了コード 1 にする(直ったのに印を外し忘れないため)。
	 * E6b で不具合を直したら、該当する行を消す。
	 */
	static constexpr const char* KNOWN_FAILURES[] = {
		// 例外 1: DeserializeFrom が先頭で slots_.clear() するので、clips の無い JSON でクリップが消える
		"T2 Animation(クリップ付き) {}",
		"T2 Animation(クリップ付き) playSpeed",
		// 例外 3: 書き出しは chunk_ の解像度、読み込み先は authorResolution_ で往復しない
		"T2 Terrain resolution",
		// 例外 3: HeightmapChunk の SetHeightScale / SetTerrainSize はハイトマップ未読込だと値を捨てる
		"T2 Terrain heightScale",
		"T2 Terrain terrainSize",
	};

	/** クリップ付きの Animation を作るための JSON(AddAnimation はパスを控えるだけでロードしない) */
	static constexpr const char* ANIMATION_CLIP_JSON = "{\"clips\":[{\"name\":\"a\",\"path\":\"x.fbx\"}]}";

	/** Animation の型名 */
	static constexpr const char* ANIMATION_TYPE_NAME = "Animation";


	/**
	 * 実行時オプション
	 */
	struct Options
	{
		bool        selfTest = false;
		std::string assetsPath;
		std::string dumpPath;
	};


	/**
	 * 試験結果の集計
	 */
	struct TestResults
	{
		uint32_t passCount  = 0;
		uint32_t failCount  = 0;
		uint32_t xfailCount = 0;
		uint32_t xpassCount = 0;
	};


	void PrintUsage()
	{
		std::printf(
			"ReflectionTest - リフレクション(Reflect / 型消去)の検査ツール\n"
			"\n"
			"  ReflectionTest --selftest [--assets <Game/Assets のパス>] [--dump <出力ファイル>]\n"
			"      T1 往復 / T2 疎な JSON を全登録型で試す。--assets を付けると\n"
			"      T3 既存アセットのダンプも行い、--dump のファイルへ書く。\n");
	}


	bool ParseArgs(const int argc, char** argv, Options& options)
	{
		for (int i = 1; i < argc; ++i) {
			const std::string arg     = argv[i];
			const bool        hasNext = (i + 1) < argc;

			if (arg == "--selftest") {
				options.selfTest = true;
			} else if (arg == "--assets" && hasNext) {
				options.assetsPath = argv[++i];
			} else if (arg == "--dump" && hasNext) {
				options.dumpPath = argv[++i];
			} else {
				std::fprintf(stderr, "不明な引数: %s\n", arg.c_str());
				return false;
			}
		}
		return true;
	}


	/** 型の表示名(typeName が無い型は displayName) */
	const char* NameOf(const ComponentMeta& meta)
	{
		return meta.typeName ? meta.typeName : meta.displayName;
	}


	bool IsKnownFailure(const std::string& testName)
	{
		for (const char* name : KNOWN_FAILURES) {
			if (testName == name) return true;
		}
		return false;
	}


	/** 1 試験の結果を 1 行で出し、集計する */
	void Report(TestResults& results, const std::string& testName, const bool passed, const std::string& detail)
	{
		const bool  known = IsKnownFailure(testName);
		const char* tag   = nullptr;
		if (passed) {
			tag = known ? "[XPASS]" : "[PASS]";
			if (known) {
				++results.xpassCount;
			} else {
				++results.passCount;
			}
		} else {
			tag = known ? "[XFAIL]" : "[FAIL]";
			if (known) {
				++results.xfailCount;
			} else {
				++results.failCount;
			}
		}

		if (detail.empty()) {
			std::printf("%s %s\n", tag, testName.c_str());
		} else {
			std::printf("%s %s — %s\n", tag, testName.c_str(), detail.c_str());
		}
	}


	/** JSON を 1 行の文字列にする(Stringify の改行と字下げを詰める。文字列中の改行はエスケープ済み) */
	std::string StringifyOneLine(const JsonValue& value)
	{
		const std::string text = aq::util::JsonSerializer::Stringify(value);
		std::string       out;
		out.reserve(text.size());
		for (size_t i = 0; i < text.size(); ++i) {
			if (text[i] != '\n') {
				out += text[i];
				continue;
			}
			while (i + 1 < text.size() && text[i + 1] == ' ') ++i;
		}
		return out;
	}


	/** 2 つの JSON が等しいか(数値は float に丸めて比べる) */
	bool JsonEquals(const JsonValue& a, const JsonValue& b)
	{
		if (a.IsNumber() && b.IsNumber()) return a.AsFloat() == b.AsFloat();
		if (a.IsNull() && b.IsNull()) return true;
		if (a.IsBool() && b.IsBool()) return a.AsBool() == b.AsBool();
		if (a.IsString() && b.IsString()) return a.AsString() == b.AsString();
		if (a.IsArray() && b.IsArray()) {
			if (a.Size() != b.Size()) return false;
			for (size_t i = 0; i < a.Size(); ++i) {
				if (!JsonEquals(a[i], b[i])) return false;
			}
			return true;
		}
		if (a.IsObject() && b.IsObject()) {
			const JsonValue::Object& objA = a.GetObject();
			const JsonValue::Object& objB = b.GetObject();
			if (objA.size() != objB.size()) return false;
			for (const auto& [key, value] : objA) {
				if (!b.Contains(key)) return false;
				if (!JsonEquals(value, b[key])) return false;
			}
			return true;
		}
		return false;
	}


	/** 違うキーの名前を並べる(失敗の詳細表示用) */
	std::string DiffKeys(const JsonValue& a, const JsonValue& b)
	{
		std::vector<std::string> keys;
		for (const auto& [key, value] : a.GetObject()) {
			if (!b.Contains(key) || !JsonEquals(value, b[key])) keys.push_back(key);
		}
		for (const auto& [key, value] : b.GetObject()) {
			if (!a.Contains(key)) keys.push_back(key);
		}
		std::sort(keys.begin(), keys.end());

		std::string out = "違うキー:";
		for (const std::string& key : keys) {
			out += " " + key;
		}
		return out;
	}


	/** 既定値の実体を作る。initJson が非 null なら、さらにそれを deserializePtr する */
	AlignedStorage MakeInstance(const TypeInfo& type, const ComponentMeta& meta, const JsonValue* initJson)
	{
		AlignedStorage storage(type);
		if (initJson) meta.deserializePtr(storage.Get(), *initJson);
		return storage;
	}


	JsonValue Serialize(const ComponentMeta& meta, const AlignedStorage& storage)
	{
		JsonValue out;
		meta.serializePtr(storage.Get(), out);
		return out;
	}


	/**
	 * 数値・真偽値・数値の配列のキーについて、変えた値を作る
	 *
	 * 数値は +1、真偽値は反転、配列は先頭の要素に +1。対象外(文字列 = パス、空配列、
	 * 数値でない要素を含む配列、オブジェクト)は false を返す。
	 */
	bool MakeChangedValue(const JsonValue& value, JsonValue& out)
	{
		if (value.IsNumber()) {
			out = JsonValue(static_cast<double>(value.AsFloat()) + 1.0);
			return true;
		}
		if (value.IsBool()) {
			out = JsonValue(!value.AsBool());
			return true;
		}
		if (value.IsArray() && value.Size() > 0) {
			for (const JsonValue& element : value.GetArray()) {
				if (!element.IsNumber()) return false;
			}
			out = JsonValue::MakeArray();
			for (size_t i = 0; i < value.Size(); ++i) {
				const double delta = (i == 0) ? 1.0 : 0.0;
				out.PushBack(JsonValue(static_cast<double>(value[i].AsFloat()) + delta));
			}
			return true;
		}
		return false;
	}


	/** T1 往復: 既定値 → serializePtr → 新しい実体へ deserializePtr → serializePtr が一致する */
	void RunRoundTrip(TestResults& results, const TypeInfo& type, const ComponentMeta& meta)
	{
		const std::string testName = std::string("T1 ") + NameOf(meta);

		AlignedStorage  source   = MakeInstance(type, meta, nullptr);
		const JsonValue first    = Serialize(meta, source);
		AlignedStorage  target   = MakeInstance(type, meta, &first);
		const JsonValue second   = Serialize(meta, target);

		const bool passed = JsonEquals(first, second);
		Report(results, testName, passed, passed ? std::string() : DiffKeys(first, second));
	}


	/**
	 * T2 疎な JSON(1 つの実体の種類について)
	 *
	 * @param label    試験名に使う型の表記(クリップ付き Animation を区別する)
	 * @param initJson 実体を作るときに入れる JSON(既定値のままなら nullptr)
	 * @param onlyKey  非 null なら、このキーだけを試す
	 */
	void RunSparse(TestResults& results, const TypeInfo& type, const ComponentMeta& meta,
	               const std::string& label, const JsonValue* initJson, const char* onlyKey)
	{
		const JsonValue baseline = Serialize(meta, MakeInstance(type, meta, initJson));

		// 空の JSON は何も変えない
		{
			AlignedStorage storage = MakeInstance(type, meta, initJson);
			meta.deserializePtr(storage.Get(), JsonValue::MakeObject());
			const JsonValue after  = Serialize(meta, storage);
			const bool      passed = JsonEquals(baseline, after);
			Report(results, "T2 " + label + " {}", passed, passed ? std::string() : DiffKeys(baseline, after));
		}

		// 各キーだけを変えた JSON は、そのキーだけを変える。キー順は決定的にする
		std::vector<std::string> keys;
		for (const auto& [key, value] : baseline.GetObject()) {
			keys.push_back(key);
		}
		std::sort(keys.begin(), keys.end());

		for (const std::string& key : keys) {
			if (onlyKey && key != onlyKey) continue;

			JsonValue changed;
			if (!MakeChangedValue(baseline[key], changed)) continue;

			JsonValue patch = JsonValue::MakeObject();
			patch.Set(key, changed);

			AlignedStorage storage = MakeInstance(type, meta, initJson);
			meta.deserializePtr(storage.Get(), patch);
			const JsonValue after = Serialize(meta, storage);

			// 期待値は基準の写しにそのキーだけを差し替えたもの
			JsonValue expected = baseline;
			expected.Set(key, changed);

			const bool passed = JsonEquals(expected, after);
			Report(results, "T2 " + label + " " + key, passed, passed ? std::string() : DiffKeys(expected, after));
		}
	}


	/** T1 / T2 を全登録型で回す */
	void RunTypeTests(TestResults& results)
	{
		const JsonValue animationClipJson = aq::util::JsonParser::ParseString(ANIMATION_CLIP_JSON);

		for (const auto& [type, meta] : ComponentRegistry::Get().GetAll()) {
			if (!meta.serializePtr || !meta.deserializePtr) {
				std::printf("        %s — serializePtr / deserializePtr が無いので T1 / T2 の対象外\n", NameOf(meta));
				continue;
			}

			RunRoundTrip(results, type, meta);
			RunSparse(results, type, meta, NameOf(meta), nullptr, nullptr);

			// Animation は既定値ではクリップが空で、clips を消す不具合を見逃すので、クリップ付きでも試す
			if (meta.typeName && std::string_view(meta.typeName) == ANIMATION_TYPE_NAME) {
				RunSparse(results, type, meta, "Animation(クリップ付き)", &animationClipJson, "playSpeed");
			}
		}
	}


	/**
	 * T3 の 1 行分
	 */
	struct DumpEntry
	{
		std::string jsonPath;
		std::string typeName;
		std::string result;
	};


	/** JSON 中の components オブジェクトを再帰で探し、各コンポーネントを書き出し直した結果を集める */
	void CollectComponents(const JsonValue& node, const std::string& jsonPath, std::vector<DumpEntry>& out)
	{
		if (node.IsArray()) {
			for (size_t i = 0; i < node.Size(); ++i) {
				CollectComponents(node[i], jsonPath + "[" + std::to_string(i) + "]", out);
			}
			return;
		}
		if (!node.IsObject()) return;

		std::vector<std::string> keys;
		for (const auto& [key, value] : node.GetObject()) {
			keys.push_back(key);
		}
		std::sort(keys.begin(), keys.end());

		for (const std::string& key : keys) {
			const JsonValue&  child     = node[key];
			const std::string childPath = jsonPath.empty() ? key : jsonPath + "." + key;

			if (key == "components" && child.IsObject()) {
				std::vector<std::string> typeNames;
				for (const auto& [typeName, value] : child.GetObject()) {
					typeNames.push_back(typeName);
				}
				std::sort(typeNames.begin(), typeNames.end());

				for (const std::string& typeName : typeNames) {
					DumpEntry entry;
					entry.jsonPath = childPath;
					entry.typeName = typeName;

					const ComponentRegistry& registry = ComponentRegistry::Get();
					const ComponentMeta*     meta     = registry.Find(typeName);
					if (!meta) {
						entry.result = "(未登録)";
					} else if (!meta->serializePtr || !meta->deserializePtr) {
						entry.result = "(serializePtr なし)";
					} else {
						AlignedStorage storage(registry.TypeOf(typeName));
						meta->deserializePtr(storage.Get(), child[typeName]);
						entry.result = StringifyOneLine(Serialize(*meta, storage));
					}
					out.push_back(std::move(entry));
				}
			}

			CollectComponents(child, childPath, out);
		}
	}


	/** T3 既存アセット: 全 *.prefab.json / *.level.json のコンポーネントを書き出し直して --dump へ書く */
	void RunAssetDump(TestResults& results, const Options& options)
	{
		namespace fs = std::filesystem;

		std::error_code   ec;
		const fs::path    root = fs::absolute(fs::path(options.assetsPath), ec);
		if (ec || !fs::is_directory(root, ec)) {
			Report(results, "T3 既存アセット", false, "--assets がディレクトリではない: " + options.assetsPath);
			return;
		}

		// ファイル順を決定的にするため、相対パスでソートする
		std::vector<std::string> relativePaths;
		for (const fs::directory_entry& entry : fs::recursive_directory_iterator(root, ec)) {
			if (!entry.is_regular_file()) continue;
			const std::string fileName = entry.path().filename().string();
			const bool isPrefab = fileName.size() > 12 && fileName.ends_with(".prefab.json");
			const bool isLevel  = fileName.size() > 11 && fileName.ends_with(".level.json");
			if (!isPrefab && !isLevel) continue;
			relativePaths.push_back(fs::relative(entry.path(), root).generic_string());
		}
		std::sort(relativePaths.begin(), relativePaths.end());

		std::string                lines;
		std::vector<std::string>   parseErrors;
		size_t                     componentCount = 0;
		for (const std::string& relativePath : relativePaths) {
			const std::string fullPath = (root / relativePath).string();
			const JsonValue   json     = aq::util::JsonParser::ParseFile(fullPath.c_str());
			if (json.IsNull()) {
				parseErrors.push_back(relativePath);
				continue;
			}

			std::vector<DumpEntry> entries;
			CollectComponents(json, std::string(), entries);
			componentCount += entries.size();
			for (const DumpEntry& entry : entries) {
				lines += relativePath + "\t" + entry.jsonPath + "\t" + entry.typeName + "\t" + entry.result + "\n";
			}
		}

		std::printf("        T3: ファイル %zu 件、コンポーネント %zu 件\n", relativePaths.size(), componentCount);

		if (!options.dumpPath.empty()) {
			std::ofstream ofs(options.dumpPath, std::ios::binary | std::ios::trunc);
			ofs << lines;
			if (!ofs.good()) {
				Report(results, "T3 既存アセット", false, "--dump に書けない: " + options.dumpPath);
				return;
			}
		}

		std::string detail;
		for (const std::string& path : parseErrors) {
			detail += (detail.empty() ? "JSON として読めない:" : "") + std::string(" ") + path;
		}
		Report(results, "T3 既存アセット", parseErrors.empty(), detail);
	}


	int RunSelfTest(const Options& options)
	{
		// コンポーネントの登録(Application::Initialize と同じ順)
		{
			ComponentRegistry::RegisterCoreComponents();
			aq::level::RegisterLevelComponents();
		}

		TestResults results;
		RunTypeTests(results);

		if (options.assetsPath.empty()) {
			std::printf("        T3: --assets が無いので行わない\n");
		} else {
			RunAssetDump(results, options);
		}

		std::printf("[selftest] PASS %u / FAIL %u / XFAIL %u / XPASS %u\n",
			results.passCount, results.failCount, results.xfailCount, results.xpassCount);
		return (results.failCount == 0 && results.xpassCount == 0) ? 0 : 1;
	}
}


int main(int argc, char** argv)
{
	Options options;
	if (!ParseArgs(argc, argv, options) || !options.selfTest) {
		PrintUsage();
		return 1;
	}
	return RunSelfTest(options);
}
