// リフレクション(Reflect と型消去の serializePtr / deserializePtr)の検査ツール
// (設計書 エディタ全体設計.md 6.4 節「5. 検査ツール」。E6a / E6b)。
//
//   ReflectionTest --selftest [--assets <Game/Assets のパス>] [--dump <出力ファイル>]
//
// T1 往復 / T2 疎な JSON / T3 既存アセットのダンプ / T4 編集イベント / T5 属性 を行う。
// E4a で T6 編集ノードの往復 / T7 数値の書き出し を足した(7.8 節「9. 検査ツールの追加」)。
// E5a で T9 実行モード(System の選択と全モードでの Flush)を足した。T8 と同じく画面を作らず、System を登録して EntityContext::Update(mode) を回す。
// E4b で T8 生成と取り消し を足した。T8 だけは Engine(Create のみ)と EntityContext を作り、
// 遅延生成を FlushPendingCommands と LevelManager::Tick で動かす(画面・デバイス・System は作らない)。
// 画面もデバイスも作らない。実体は AlignedStorage(TypeInfo) で既定値のまま作り、
// ComponentMeta の型消去関数だけを通す。T4 / T5 は画面を持たない ImGui のコンテキストに
// 入力を合成して行う(ImGui の Visitor は AQ_DEBUG_IMGUI のときだけあるので Debug 構成のみ)。
// T6 と T7 のファイル往復も、編集ノード(PrefabEditNode)が AQ_DEBUG_IMGUI のときだけあるので Debug 構成のみ。
#include "aq.h"
#include "ECS/ComponentRegistry.h"
#include "ECS/AlignedStorage.h"
#include "Level/LevelComponentRegistry.h"
#include "Level/LevelComponents.h"
#include "Level/LevelManager.h"
#include "Level/LevelRegistry.h"
#include "Level/LevelSerializer.h"
#include "Util/SimpleJson.h"
#ifdef AQ_DEBUG_IMGUI
#include "Level/LevelEditModel.h"
#include "ECS/PrefabEditor.h"
#include "ECS/PrefabEditNodeOps.h"
#include "ECS/ImGuiFieldVisitor.h"
#include "ECS/ImGuiEditEventVisitor.h"
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>   // T4 (7) で ImGuiContext::AnyIdHasBeenEditedThisFrame を直接立てる
#endif

#include <array>
#include <atomic>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
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
	 * 既知の失敗(エンジンの不具合。README の「既知の課題」)
	 *
	 * ここに名前がある試験は、失敗しても [XFAIL] として終了コードに数えない。
	 * 通ったら [XPASS] を出して終了コード 1 にする(直ったのに印を外し忘れないため)。
	 * 不具合を直したら、該当する行を消す。
	 * E6a の 5 件(設計書 6.4 節の例外 1・例外 3)は E6b で直したので、今は無い。
	 * E4a の前に控えた T6 / T7 の 18 件(override が増える・子の override が消える・数値が float に狭まる)は
	 * E4a で直したので外した(一覧は README の「既知の課題」)。
	 */
	static constexpr std::array<const char*, 0> KNOWN_FAILURES = {};

	/** クリップ付きの Animation を作るための JSON(AddAnimation はパスを控えるだけでロードしない) */
	static constexpr const char* ANIMATION_CLIP_JSON = "{\"clips\":[{\"name\":\"a\",\"path\":\"x.fbx\"}]}";

	/** Animation の型名 */
	static constexpr const char* ANIMATION_TYPE_NAME = "Animation";

	/** T6 / T7 の試験用データの場所(CMakeLists.txt がソースの位置を埋め込む) */
#ifdef REFLECTION_TEST_DATA_DIR
	static constexpr const char* TEST_DATA_DIR = REFLECTION_TEST_DATA_DIR;
#else
	static constexpr const char* TEST_DATA_DIR = "Tools/ReflectionTest/TestData";
#endif


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
			"      T3 既存アセットのダンプも行い、--dump のファイルへ書く。\n"
			"      Debug 構成(AQ_DEBUG_IMGUI)では T4 編集イベント / T5 属性 も画面無しの ImGui で試す。\n"
			"      T7 数値の書き出しを試す。Debug 構成では T6 編集ノードの往復(TestData と --assets)と\n"
			"      T7 のファイル往復も行う。\n"
			"      T8 Level の生成と取り消し(IsSpawned / Unload / LoadAsync の取り消し / Unregister)を試す。\n"
			"      Debug 構成ではプレビューの置き換え(LevelEditModel)も行う。\n"
			"      T9 実行モード(Edit / Play / Pause で動く System と、全モードでの Flush)を試す。\n");
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


	/** 2 つの JSON が構造で等しいか(キーの順序は無視、キーの有無も一致、数値は double で完全一致。11.4 節) */
	bool JsonEqualsExact(const JsonValue& a, const JsonValue& b)
	{
		if (a.IsNumber() && b.IsNumber()) return a.AsDouble() == b.AsDouble();
		if (a.IsNull() && b.IsNull()) return true;
		if (a.IsBool() && b.IsBool()) return a.AsBool() == b.AsBool();
		if (a.IsString() && b.IsString()) return a.AsString() == b.AsString();
		if (a.IsArray() && b.IsArray()) {
			if (a.Size() != b.Size()) return false;
			for (size_t i = 0; i < a.Size(); ++i) {
				if (!JsonEqualsExact(a[i], b[i])) return false;
			}
			return true;
		}
		if (a.IsObject() && b.IsObject()) {
			const JsonValue::Object& objA = a.GetObject();
			const JsonValue::Object& objB = b.GetObject();
			if (objA.size() != objB.size()) return false;
			for (const auto& [key, value] : objA) {
				if (!b.Contains(key)) return false;
				if (!JsonEqualsExact(value, b[key])) return false;
			}
			return true;
		}
		return false;
	}


	/**
	 * T7 の Stringify の 1 件
	 */
	struct NumberCase
	{
		/** 試験名に使う表記 */
		const char* label;
		/** 読み込む JSON の数値の文字列(nullptr なら floatValue を double に広げた値を使う) */
		const char* inputText;
		float       floatValue;
		/** 期待する書き出しの表記 */
		const char* expected;
		/** true = 読み直して double として一致、false = float として一致 */
		bool        compareAsDouble;
	};


	/**
	 * T7 の表記の期待値(7.8 節「1. 数値」の 3 分岐)
	 *
	 * 2^53 と 2^53 + 1(double では 2^53 に丸まる)は「整数で絶対値が 2^53 未満」に当たらず、
	 * float に狭めても同じ値なので float の最短表現になる。double としては変わるので float として比べる。
	 * -0.0 は整数の分岐で "0" になる(double の == では 0.0 と等しい)。
	 */
	static constexpr std::array<NumberCase, 10> NUMBER_CASES = { {
		{ "0.1f",             nullptr,              0.1f, "0.1",          false },
		{ "1234567.5",        "1234567.5",          0.0f, "1234567.5",    true  },
		{ "16777217",         "16777217",           0.0f, "16777217",     true  },
		{ "2147483647",       "2147483647",         0.0f, "2147483647",   true  },
		{ "-2147483648",      "-2147483648",        0.0f, "-2147483648",  true  },
		{ "9007199254740992", "9007199254740992",   0.0f, "9.007199e+15", false },
		{ "9007199254740993", "9007199254740993",   0.0f, "9.007199e+15", false },
		{ "1e20",             "1e20",               0.0f, "1e+20",        true  },
		{ "1e-05",            "1e-05",              0.0f, "1e-05",        true  },
		{ "-0.0",             "-0.0",               0.0f, "0",            true  },
	} };


	/** T7 数値(表記): Stringify の表記と、読み直した値が一致する */
	void RunNumberFormatTests(TestResults& results)
	{
		for (const NumberCase& numberCase : NUMBER_CASES) {
			const JsonValue   value   = numberCase.inputText
				? aq::util::JsonParser::ParseString(numberCase.inputText)
				: JsonValue(static_cast<double>(numberCase.floatValue));
			const std::string text    = aq::util::JsonSerializer::Stringify(value);
			const JsonValue   reread  = aq::util::JsonParser::ParseString(text);

			const bool sameText  = text == numberCase.expected;
			const bool sameValue = numberCase.compareAsDouble
				? reread.AsDouble() == value.AsDouble()
				: reread.AsFloat() == value.AsFloat();

			char detail[160];
			std::snprintf(detail, sizeof(detail), "表記 \"%s\"(期待 \"%s\")、読み直し %.17g(元 %.17g、%s で比較)",
				text.c_str(), numberCase.expected, reread.AsDouble(), value.AsDouble(),
				numberCase.compareAsDouble ? "double" : "float");
			const bool passed = sameText && sameValue;
			Report(results, std::string("T7 Stringify ") + numberCase.label, passed, passed ? std::string() : detail);
		}
	}


#ifdef AQ_DEBUG_IMGUI
	using aq::ecs::PrefabEditComponent;
	using aq::ecs::PrefabEditNode;

	/** T6 の (a)〜(g) の編集試験に使う Level と、その中の参照ノードの名前(外側の name) */
	static constexpr const char* OVERRIDE_TEST_LEVEL = "T6_Overrides.level.json";
	static constexpr const char* OVERRIDE_TEST_NODE  = "RefFull";
	/** T7 のファイル往復に使う Level と、その中のノードの名前 */
	static constexpr const char* NUMBER_TEST_LEVEL   = "T7_Numbers.level.json";
	static constexpr const char* NUMBER_FLOAT_NODE   = "Precise";
	static constexpr const char* NUMBER_DOUBLE_NODE  = "RefDouble";
	/** T7 で書き出す一時ファイルの名前(一時ディレクトリに置き、読み直したら消す) */
	static constexpr const char* NUMBER_TEMP_FILE    = "ReflectionTest_T7_Numbers.level.json";


	/** ディレクトリ以下の *.prefab.json / *.level.json の相対パス(名前順) */
	std::vector<std::string> ListPrefabAndLevelFiles(const std::filesystem::path& root)
	{
		namespace fs = std::filesystem;

		std::vector<std::string> relativePaths;
		std::error_code          ec;
		for (const fs::directory_entry& entry : fs::recursive_directory_iterator(root, ec)) {
			if (!entry.is_regular_file()) continue;
			const std::string fileName = entry.path().filename().string();
			const bool isPrefab = fileName.size() > 12 && fileName.ends_with(".prefab.json");
			const bool isLevel  = fileName.size() > 11 && fileName.ends_with(".level.json");
			if (!isPrefab && !isLevel) continue;
			relativePaths.push_back(fs::relative(entry.path(), root).generic_string());
		}
		std::sort(relativePaths.begin(), relativePaths.end());
		return relativePaths;
	}


	bool IsReferenceNode(const JsonValue& node)
	{
		return node.Contains("prefab") && node["prefab"].IsString();
	}


	/** 参照ノードの overrides(無ければ空のオブジェクト) */
	const JsonValue& OverridesOf(const JsonValue& node)
	{
		static const JsonValue EMPTY_OBJECT = JsonValue::MakeObject();
		const JsonValue& overrides = node["overrides"];
		return overrides.IsObject() ? overrides : EMPTY_OBJECT;
	}


	/** ノードの実行時の名前(参照ノードは overrides.name を優先する。PrefabSerializer の ApplyPatch と同じ) */
	std::string EffectiveName(const JsonValue& node)
	{
		if (IsReferenceNode(node)) {
			const JsonValue& overrides = OverridesOf(node);
			if (overrides.Contains("name") && overrides["name"].IsString()) return overrides["name"].AsString();
		}
		return node.Contains("name") ? node["name"].AsString() : std::string();
	}


	/**
	 * 実体ノードのコンポーネントを比べられる形にする(11.4 節)
	 *
	 * 既定値の実体へ deserializePtr してから serializePtr する(省略されたフィールドは既定値で埋まる)。
	 * 型消去の関数が無い型(未登録を含む)は、元の値のまま残す(書き出しで消えたら不一致になる)。
	 */
	JsonValue CanonicalComponents(const JsonValue& components)
	{
		const ComponentRegistry& registry = ComponentRegistry::Get();
		JsonValue out = JsonValue::MakeObject();
		for (const auto& [typeName, value] : components.GetObject()) {
			const ComponentMeta* meta = registry.Find(typeName);
			if (!meta || !meta->serializePtr || !meta->deserializePtr) {
				out.Set(typeName, value);
				continue;
			}
			AlignedStorage storage(registry.TypeOf(typeName));
			meta->deserializePtr(storage.Get(), value);
			out.Set(typeName, Serialize(*meta, storage));
		}
		return out;
	}


	/**
	 * 元のノードと書き出したノードを 11.4 節の規則で比べる
	 *
	 * 参照ノード: prefab のパスと overrides の生 JSON を構造で比べる(キーの有無も一致、数値は double で完全一致)。
	 * 実体ノード: コンポーネントを既定値で構築して比べ、子の順序と数を再帰で比べる。
	 * ノード名は実行時の名前(参照ノードは overrides.name 優先)で比べる。
	 */
	bool CompareNode(const JsonValue& original, const JsonValue& written, const std::string& path, std::string& why)
	{
		const std::string where = path.empty() ? std::string("(ノード)") : path;

		if (EffectiveName(original) != EffectiveName(written)) {
			why = where + " の名前: \"" + EffectiveName(original) + "\" → \"" + EffectiveName(written) + "\"";
			return false;
		}

		const bool isReference = IsReferenceNode(original);
		if (isReference != IsReferenceNode(written)) {
			why = where + " の参照ノードかどうかが変わった";
			return false;
		}

		if (isReference) {
			if (original["prefab"].AsString() != written["prefab"].AsString()) {
				why = where + " の prefab: \"" + original["prefab"].AsString() + "\" → \"" + written["prefab"].AsString() + "\"";
				return false;
			}
			if (!JsonEqualsExact(OverridesOf(original), OverridesOf(written))) {
				why = where + " の overrides: 元 " + StringifyOneLine(OverridesOf(original))
				    + " / 書き出し " + StringifyOneLine(OverridesOf(written));
				return false;
			}
			return true;
		}

		const JsonValue& originalComponents = original["components"];
		const JsonValue& writtenComponents  = written["components"];
		const JsonValue  canonicalOriginal  = CanonicalComponents(originalComponents);
		const JsonValue  canonicalWritten   = CanonicalComponents(writtenComponents);
		if (!JsonEqualsExact(canonicalOriginal, canonicalWritten)) {
			why = where + " の components: 元 " + StringifyOneLine(canonicalOriginal)
			    + " / 書き出し " + StringifyOneLine(canonicalWritten);
			return false;
		}

		const JsonValue& originalChildren = original["children"];
		const JsonValue& writtenChildren  = written["children"];
		if (originalChildren.Size() != writtenChildren.Size()) {
			why = where + " の子の数: " + std::to_string(originalChildren.Size()) + " → " + std::to_string(writtenChildren.Size());
			return false;
		}
		for (size_t i = 0; i < originalChildren.Size(); ++i) {
			const std::string childPath = where + ".children[" + std::to_string(i) + "]";
			if (!CompareNode(originalChildren[i], writtenChildren[i], childPath, why)) return false;
		}
		return true;
	}


	/** ノード 1 つを編集ノードへ読み込んで書き出し、元と比べる */
	void RunNodeRoundTrip(TestResults& results, const std::string& testName, const JsonValue& source)
	{
		const std::unique_ptr<PrefabEditNode> node    = aq::ecs::PrefabNodeFromJson(source);
		const JsonValue                       written = aq::ecs::PrefabNodeToJson(*node);
		std::string why;
		const bool passed = CompareNode(source, written, std::string(), why);
		Report(results, testName, passed, why);
	}


	/** 1 ファイルのノード単位の往復(Level は entities の各ノード、Prefab はルート) */
	void RunNodeRoundTripFile(TestResults& results, const std::string& label, const std::string& fullPath)
	{
		const JsonValue json = aq::util::JsonParser::ParseFile(fullPath.c_str());
		if (!json.IsObject()) {
			Report(results, "T6 " + label, false, "JSON として読めない: " + fullPath);
			return;
		}

		const bool isLevel = label.ends_with(".level.json");
		if (!isLevel) {
			RunNodeRoundTrip(results, "T6 " + label + " ルート", json);
			return;
		}

		const JsonValue& entities = json["entities"];
		for (size_t i = 0; i < entities.Size(); ++i) {
			RunNodeRoundTrip(results, "T6 " + label + " entities[" + std::to_string(i) + "]", entities[i]);
		}
	}


	/** ディレクトリ以下の全 *.prefab.json / *.level.json でノード単位の往復を行う */
	void RunNodeRoundTripDirectory(TestResults& results, const std::string& labelPrefix, const std::string& directory)
	{
		namespace fs = std::filesystem;

		std::error_code ec;
		const fs::path  root = fs::absolute(fs::path(directory), ec);
		if (ec || !fs::is_directory(root, ec)) {
			Report(results, "T6 " + labelPrefix, false, "ディレクトリではない: " + directory);
			return;
		}
		for (const std::string& relativePath : ListPrefabAndLevelFiles(root)) {
			RunNodeRoundTripFile(results, labelPrefix + "/" + relativePath, (root / relativePath).string());
		}
	}


	/** Level の entities から、外側の name が一致するノードを探す */
	const JsonValue* FindEntity(const JsonValue& level, const char* name)
	{
		const JsonValue& entities = level["entities"];
		for (size_t i = 0; i < entities.Size(); ++i) {
			if (entities[i]["name"].IsString() && entities[i]["name"].AsString() == name) return &entities[i];
		}
		return nullptr;
	}


	PrefabEditComponent* FindEditComponent(PrefabEditNode& node, const char* typeName)
	{
		const TypeInfo type = ComponentRegistry::Get().TypeOf(typeName);
		for (PrefabEditComponent& component : node.components) {
			if (component.data.Type() == type) return &component;
		}
		return nullptr;
	}


	/** Inspector の「Remove Component」と同じく一覧から外す。外したら true */
	bool RemoveEditComponent(PrefabEditNode& node, const char* typeName)
	{
		const TypeInfo type = ComponentRegistry::Get().TypeOf(typeName);
		for (size_t i = 0; i < node.components.size(); ++i) {
			if (node.components[i].data.Type() == type) {
				node.components.erase(node.components.begin() + static_cast<std::ptrdiff_t>(i));
				return true;
			}
		}
		return false;
	}


	/** Inspector の「+ Add Component」と同じく既定値の実体を足す */
	PrefabEditComponent& AddEditComponent(PrefabEditNode& node, const char* typeName)
	{
		node.components.push_back(PrefabEditComponent{ AlignedStorage(ComponentRegistry::Get().TypeOf(typeName)) });
		return node.components.back();
	}


	/** フィールドの編集の代わりに、疎な JSON を deserializePtr する */
	void PatchEditComponent(PrefabEditComponent& component, const char* jsonText)
	{
		const ComponentMeta* meta = ComponentRegistry::Get().Find(component.data.Type().GetHash());
		meta->deserializePtr(component.data.Get(), aq::util::JsonParser::ParseString(jsonText));
	}


	JsonValue SerializeEditComponent(const PrefabEditComponent& component)
	{
		const ComponentMeta* meta = ComponentRegistry::Get().Find(component.data.Type().GetHash());
		return Serialize(*meta, component.data);
	}


	/** オブジェクトの key の中の member を value にした写しを返す(key が無ければ作る) */
	JsonValue WithMember(const JsonValue& object, const char* key, const char* member, const JsonValue& value)
	{
		JsonValue inner = object[key].IsObject() ? object[key] : JsonValue::MakeObject();
		inner.Set(member, value);
		JsonValue out = object;
		out.Set(key, std::move(inner));
		return out;
	}


	/** オブジェクトの key の中から member を除いた写しを返す */
	JsonValue WithoutMember(const JsonValue& object, const char* key, const char* member)
	{
		JsonValue inner = JsonValue::MakeObject();
		for (const auto& [name, value] : object[key].GetObject()) {
			if (name != member) inner.Set(name, value);
		}
		JsonValue out = object;
		out.Set(key, std::move(inner));
		return out;
	}


	/** 書き出したノードをテキストにして読み直し、編集ノードへ読み込む(保存して読み直すのと同じ) */
	std::unique_ptr<PrefabEditNode> RereadNode(const JsonValue& written)
	{
		const std::string text = aq::util::JsonSerializer::Stringify(written);
		return aq::ecs::PrefabNodeFromJson(aq::util::JsonParser::ParseString(text));
	}


	/** 書き出した overrides を期待値と比べて報告する */
	void ReportOverrides(TestResults& results, const std::string& testName, const JsonValue& written, const JsonValue& expected)
	{
		const JsonValue& actual = OverridesOf(written);
		const bool passed = JsonEqualsExact(actual, expected);
		Report(results, testName, passed,
			passed ? std::string() : "期待 " + StringifyOneLine(expected) + " / 書き出し " + StringifyOneLine(actual));
	}


	/** T6 (a)〜(g): 参照ノードをプログラムで編集してから書き出す */
	void RunOverrideEditTests(TestResults& results)
	{
		const std::string levelPath = std::string(TEST_DATA_DIR) + "/" + OVERRIDE_TEST_LEVEL;
		const JsonValue   level     = aq::util::JsonParser::ParseFile(levelPath.c_str());
		const JsonValue*  source    = FindEntity(level, OVERRIDE_TEST_NODE);
		if (!source) {
			Report(results, "T6 編集試験", false, std::string("試験用のノードが無い: ") + levelPath + " " + OVERRIDE_TEST_NODE);
			return;
		}
		const JsonValue& raw = OverridesOf(*source);

		// (a) override のフィールドを変える → そのキーだけが増える(override に無かった scale を変える)
		{
			const std::string name = "T6 (a) override のフィールドを変える";
			std::unique_ptr<PrefabEditNode> node      = aq::ecs::PrefabNodeFromJson(*source);
			PrefabEditComponent*            transform = FindEditComponent(*node, "Transform");
			if (!transform) {
				Report(results, name, false, "Transform が読み込まれていない");
			} else {
				PatchEditComponent(*transform, "{\"scale\":[2,2,2]}");
				const JsonValue written       = aq::ecs::PrefabNodeToJson(*node);
				const JsonValue transformRaw  = raw["components"]["Transform"];
				JsonValue       transformWant = transformRaw;
				transformWant.Set("scale", aq::util::JsonParser::ParseString("[2,2,2]"));
				ReportOverrides(results, name, written, WithMember(raw, "components", "Transform", transformWant));
			}
		}

		// (b) 変えて元に戻す → 元と同じ
		{
			const std::string name = "T6 (b) 変えて元に戻す";
			std::unique_ptr<PrefabEditNode> node      = aq::ecs::PrefabNodeFromJson(*source);
			PrefabEditComponent*            transform = FindEditComponent(*node, "Transform");
			if (!transform) {
				Report(results, name, false, "Transform が読み込まれていない");
			} else {
				const JsonValue before = SerializeEditComponent(*transform);
				PatchEditComponent(*transform, "{\"position\":[7,7,7],\"scale\":[4,4,4]}");
				const ComponentMeta* meta = ComponentRegistry::Get().Find(transform->data.Type().GetHash());
				meta->deserializePtr(transform->data.Get(), before);
				ReportOverrides(results, name, aq::ecs::PrefabNodeToJson(*node), raw);
			}
		}

		// (c) override コンポーネントを外す → components から消える
		{
			const std::string name = "T6 (c) override コンポーネントを外す";
			std::unique_ptr<PrefabEditNode> node = aq::ecs::PrefabNodeFromJson(*source);
			if (!RemoveEditComponent(*node, "Spawner")) {
				Report(results, name, false, "Spawner が読み込まれていない");
			} else {
				ReportOverrides(results, name, aq::ecs::PrefabNodeToJson(*node), WithoutMember(raw, "components", "Spawner"));
			}
		}

		// (c) 外して同じ型を足し直す → 新しく足したコンポーネントとして全フィールドが components に入る
		{
			const std::string name = "T6 (c) 外して同じ型を足し直す";
			std::unique_ptr<PrefabEditNode> node = aq::ecs::PrefabNodeFromJson(*source);
			if (!RemoveEditComponent(*node, "Spawner")) {
				Report(results, name, false, "Spawner が読み込まれていない");
			} else {
				const JsonValue added = SerializeEditComponent(AddEditComponent(*node, "Spawner"));
				ReportOverrides(results, name, aq::ecs::PrefabNodeToJson(*node), WithMember(raw, "components", "Spawner", added));
			}
		}

		// (d) addedComponents のコンポーネントを変える → addedComponents 側が全フィールドで変わる
		{
			const std::string name = "T6 (d) addedComponents のコンポーネントを変える";
			std::unique_ptr<PrefabEditNode> node  = aq::ecs::PrefabNodeFromJson(*source);
			PrefabEditComponent*            audio = FindEditComponent(*node, "AudioSource");
			if (!audio) {
				Report(results, name, false, "addedComponents の AudioSource が読み込まれていない");
			} else {
				PatchEditComponent(*audio, "{\"volume\":0.25}");
				const JsonValue current = SerializeEditComponent(*audio);
				ReportOverrides(results, name, aq::ecs::PrefabNodeToJson(*node), WithMember(raw, "addedComponents", "AudioSource", current));
			}
		}

		// (e) Name を変える → 外側の name と overrides.name の両方が変わる
		{
			const std::string name    = "T6 (e) Name を変える";
			const std::string newName = "RenamedByTest";
			std::unique_ptr<PrefabEditNode> node = aq::ecs::PrefabNodeFromJson(*source);
			node->name = newName;
			const JsonValue written  = aq::ecs::PrefabNodeToJson(*node);
			JsonValue       expected = raw;
			expected.Set("name", JsonValue(newName));
			const bool outerOk = written["name"].IsString() && written["name"].AsString() == newName;
			if (!outerOk) {
				Report(results, name, false, "外側の name: \"" + written["name"].AsString() + "\"");
			} else {
				ReportOverrides(results, name, written, expected);
			}
		}

		// (f) addedComponents のコンポーネントを外す → 保存して読み直しても復活しない
		{
			const std::string name = "T6 (f) addedComponents のコンポーネントを外す";
			std::unique_ptr<PrefabEditNode> node = aq::ecs::PrefabNodeFromJson(*source);
			if (!RemoveEditComponent(*node, "AudioSource")) {
				Report(results, name, false, "addedComponents の AudioSource が読み込まれていない");
			} else {
				const JsonValue                 written = aq::ecs::PrefabNodeToJson(*node);
				std::unique_ptr<PrefabEditNode> reread  = RereadNode(written);
				const bool passed = FindEditComponent(*reread, "AudioSource") == nullptr
					&& !OverridesOf(written)["addedComponents"].Contains("AudioSource")
					&& !OverridesOf(written)["components"].Contains("AudioSource");
				Report(results, name, passed, passed ? std::string() : "書き出し " + StringifyOneLine(OverridesOf(written)));
			}
		}

		// (g) addedComponents のコンポーネントを外し、同じ型を足し直して値を変える → 読み直すと新しい値
		{
			const std::string name = "T6 (g) addedComponents を外して足し直す";
			std::unique_ptr<PrefabEditNode> node = aq::ecs::PrefabNodeFromJson(*source);
			if (!RemoveEditComponent(*node, "AudioSource")) {
				Report(results, name, false, "addedComponents の AudioSource が読み込まれていない");
			} else {
				PatchEditComponent(AddEditComponent(*node, "AudioSource"), "{\"volume\":0.75}");
				const JsonValue                 written = aq::ecs::PrefabNodeToJson(*node);
				std::unique_ptr<PrefabEditNode> reread  = RereadNode(written);
				const PrefabEditComponent*      audio   = FindEditComponent(*reread, "AudioSource");
				// addedComponents は後から丸ごと上書きする意味なので、古い値が残っていると新しい値が負ける
				const bool passed = audio != nullptr
					&& SerializeEditComponent(*audio)["volume"].AsFloat() == 0.75f
					&& !OverridesOf(written)["addedComponents"].Contains("AudioSource");
				Report(results, name, passed, passed ? std::string() : "書き出し " + StringifyOneLine(OverridesOf(written)));
			}
		}
	}


	/**
	 * T6 Level 全体の往復(E4b は LevelEditModel 経由。7.8 節「9.」)
	 *
	 * T6_Overrides.level.json(実体ノード・override 入りの参照ノード・2 段のインライン subLevel を含む)を
	 * LevelEditModel::FromJson → ToJson し、(1) 1 回目の結果と、それを読み直してもう一度書き出した
	 * 2 回目の結果が一致する(安定している)こと、(2) 入れ子の subLevel(Inner1 の中の Inner2)が
	 * 捨てられずに残ることを確かめる。ノード単位の中身は RunNodeRoundTripTests が既に確認済みなので、
	 * ここでは LevelEditModel 固有の関心(Level 全体の構造・入れ子の subLevel)だけを見る。
	 */
	void RunLevelModelRoundTripTest(TestResults& results)
	{
		const std::string path = std::string(TEST_DATA_DIR) + "/" + OVERRIDE_TEST_LEVEL;

		aq::level::LevelEditModel model;
		if (!model.LoadFile(path)) {
			Report(results, "T6 LevelEditModel 往復", false, "読み込み失敗: " + path);
			return;
		}
		const JsonValue first = model.ToJson();

		aq::level::LevelEditModel model2;
		if (!model2.FromJson(first)) {
			Report(results, "T6 LevelEditModel 往復", false, "2 回目の読み込みに失敗");
			return;
		}
		const JsonValue second = model2.ToJson();

		const bool stable = JsonEquals(first, second);
		Report(results, "T6 LevelEditModel 往復(安定)", stable,
			stable ? std::string() : "1 回目と 2 回目で違う: " + DiffKeys(first, second));

		// 入れ子の subLevel(Inner1 の中の Inner2)が残っているか。
		bool foundInner2 = false;
		if (first.Contains("subLevels") && first["subLevels"].IsArray()) {
			for (const JsonValue& sub : first["subLevels"].GetArray()) {
				if (!sub.Contains("level") || !sub["level"].IsObject()) continue;
				const JsonValue& inner1 = sub["level"];
				if (!inner1.Contains("subLevels") || !inner1["subLevels"].IsArray()) continue;
				for (const JsonValue& inner : inner1["subLevels"].GetArray()) {
					if (inner.Contains("level") && inner["level"].IsObject()
						&& inner["level"].Contains("name") && inner["level"]["name"].AsString() == "Inner2") {
						foundInner2 = true;
					}
				}
			}
		}
		Report(results, "T6 LevelEditModel 往復(入れ子の subLevel)", foundInner2,
			foundInner2 ? std::string() : "Inner2 が見つからない(入れ子の subLevel が捨てられている)");
	}


	/** T6 Level・Prefab の往復(E4a はノード単位、E4b は Level 全体を LevelEditModel 経由で) */
	void RunNodeRoundTripTests(TestResults& results, const Options& options)
	{
		RunNodeRoundTripDirectory(results, "TestData", TEST_DATA_DIR);
		if (options.assetsPath.empty()) {
			std::printf("        T6: --assets が無いので既存アセットは行わない\n");
		} else {
			RunNodeRoundTripDirectory(results, "Assets", options.assetsPath);
		}
		RunOverrideEditTests(results);
		RunLevelModelRoundTripTest(results);
	}


	/** source の各キーの値が、reread の同じキーと float として一致するか(reread の既定値の書き足しは見ない) */
	bool FieldsEqualAsFloat(const JsonValue& source, const JsonValue& reread, std::string& why)
	{
		for (const auto& [key, value] : source.GetObject()) {
			if (!JsonEquals(value, reread[key])) {
				why = key + ": 元 " + StringifyOneLine(value) + " / 読み直し " + StringifyOneLine(reread[key]);
				return false;
			}
		}
		return true;
	}


	/** T7 数値(ファイル): 編集ノードを通して書いたファイルを読み直し、float は float として、override は double として一致する */
	void RunNumberFileTests(TestResults& results)
	{
		namespace fs = std::filesystem;

		const std::string sourcePath = std::string(TEST_DATA_DIR) + "/" + NUMBER_TEST_LEVEL;
		const JsonValue   source     = aq::util::JsonParser::ParseFile(sourcePath.c_str());
		const JsonValue*  floatNode  = FindEntity(source, NUMBER_FLOAT_NODE);
		const JsonValue*  doubleNode = FindEntity(source, NUMBER_DOUBLE_NODE);
		if (!floatNode || !doubleNode) {
			Report(results, "T7 ファイル往復", false, "試験用のノードが無い: " + sourcePath);
			return;
		}

		// 編集ノードへ読み込んで書き出す(Level エディタの保存と同じく entities の各ノードを PrefabNodeToJson)
		JsonValue level    = JsonValue::MakeObject();
		JsonValue entities = JsonValue::MakeArray();
		for (const JsonValue& entity : source["entities"].GetArray()) {
			entities.PushBack(aq::ecs::PrefabNodeToJson(*aq::ecs::PrefabNodeFromJson(entity)));
		}
		level.Set("name", source["name"]);
		level.Set("entities", std::move(entities));

		// ファイルへ書いて読み直す
		std::error_code   ec;
		const fs::path    tempPath = fs::temp_directory_path(ec) / NUMBER_TEMP_FILE;
		const std::string tempText = tempPath.string();
		if (ec || !aq::util::JsonSerializer::WriteFile(tempText.c_str(), level)) {
			Report(results, "T7 ファイル往復", false, "一時ファイルに書けない: " + tempText);
			return;
		}
		const JsonValue reread = aq::util::JsonParser::ParseFile(tempText.c_str());
		fs::remove(tempPath, ec);

		// 通常のフィールド(float)
		{
			const JsonValue* rereadNode = FindEntity(reread, NUMBER_FLOAT_NODE);
			std::string      why;
			bool             passed     = rereadNode != nullptr;
			if (!passed) why = "読み直したファイルにノードが無い";
			for (const auto& [typeName, value] : (*floatNode)["components"].GetObject()) {
				if (!passed) break;
				passed = FieldsEqualAsFloat(value, (*rereadNode)["components"][typeName], why);
				if (!passed) why = typeName + "." + why;
			}
			Report(results, "T7 ファイル往復 float のフィールド", passed, why);
		}

		// override の値(double)
		{
			const JsonValue* rereadNode = FindEntity(reread, NUMBER_DOUBLE_NODE);
			const bool passed = rereadNode != nullptr && JsonEqualsExact(OverridesOf(*doubleNode), OverridesOf(*rereadNode));
			Report(results, "T7 ファイル往復 override の double", passed,
				passed ? std::string()
				       : "元 " + StringifyOneLine(OverridesOf(*doubleNode))
				         + " / 読み直し " + (rereadNode ? StringifyOneLine(OverridesOf(*rereadNode)) : std::string("(ノードなし)")));
		}
	}
#endif


	using aq::level::LevelId;
	using aq::level::LevelLoadHandle;
	using aq::level::LevelManager;
	using aq::level::LevelMemberComponent;
	using aq::level::LevelRegistry;

	/** T8 の (c) / (d) で 1 フレームに作るエンティティ数 */
	static constexpr uint32_t T8_ASYNC_PER_FRAME  = 1;
	/** T8 で非同期ロードを最後まで進めるときの Tick の上限(無限ループ防止) */
	static constexpr int      T8_MAX_TICKS        = 64;
	/** T8 (f) で回すフレーム数と、1 フレームに出す要求の数 */
	static constexpr int      T8_PREVIEW_FRAMES   = 10;
	static constexpr int      T8_PREVIEW_REQUESTS = 10;


	/**
	 * T8 の試験用 Level の JSON(Transform だけ)
	 *
	 * @param entityCount      トップレベルの entity の数
	 * @param inlineChildCount 0 より大きければ、その数の entity を持つインラインのサブ Level を 1 つ足す(loadOnStart)
	 */
	JsonValue MakeSpawnTestLevelJson(const char* name, const int entityCount, const int inlineChildCount)
	{
		const auto makeEntities = [](const char* prefix, const int count)
			{
				JsonValue entities = JsonValue::MakeArray();
				for (int i = 0; i < count; ++i) {
					JsonValue components = JsonValue::MakeObject();
					components.Set("Transform", JsonValue::MakeObject());
					JsonValue entity = JsonValue::MakeObject();
					entity.Set("name", JsonValue(std::string(prefix) + std::to_string(i)));
					entity.Set("components", std::move(components));
					entities.PushBack(std::move(entity));
				}
				return entities;
			};

		JsonValue level = JsonValue::MakeObject();
		level.Set("name", JsonValue(std::string(name)));
		level.Set("entities", makeEntities("E", entityCount));
		if (inlineChildCount > 0) {
			JsonValue child = JsonValue::MakeObject();
			child.Set("name", JsonValue(std::string(name) + "_Child"));
			child.Set("entities", makeEntities("C", inlineChildCount));
			JsonValue sub = JsonValue::MakeObject();
			sub.Set("level", std::move(child));
			sub.Set("loadOnStart", JsonValue(true));
			JsonValue subLevels = JsonValue::MakeArray();
			subLevels.PushBack(std::move(sub));
			level.Set("subLevels", std::move(subLevels));
		}
		return level;
	}


	/** 試験用 Level を in-memory 登録する。登録できたら true */
	bool RegisterSpawnTestLevel(const std::string& key, const int entityCount, const int inlineChildCount)
	{
		const JsonValue json = MakeSpawnTestLevelJson(key.c_str(), entityCount, inlineChildCount);
		std::shared_ptr<const aq::level::LevelData> data = aq::level::LevelSerializer::FromJson(json);
		if (!data) return false;
		LevelRegistry::Get().Register(key, data);
		return LevelRegistry::Get().IsRegistered(key);
	}


	/** ワールドのエンティティ数(T8 のエンティティはすべて Transform を持つ) */
	uint32_t CountEntities()
	{
		uint32_t count = 0;
		aq::ecs::Foreach<aq::ecs::TransformComponent>([&count](const aq::ecs::Entity&, aq::ecs::TransformComponent*)
			{
				++count;
			});
		return count;
	}


	/** levelId の Level に属するエンティティ数 */
	uint32_t CountLevelEntities(const LevelId levelId)
	{
		uint32_t count = 0;
		aq::ecs::Foreach<LevelMemberComponent>([&count, levelId](const aq::ecs::Entity&, LevelMemberComponent* member)
			{
				if (member->levelId == levelId) ++count;
			});
		return count;
	}


	/** どのロード済み Level にも属さないエンティティ数 */
	uint32_t CountOrphanEntities()
	{
		uint32_t owned = 0;
		aq::ecs::Foreach<LevelMemberComponent>([&owned](const aq::ecs::Entity&, LevelMemberComponent* member)
			{
				if (LevelManager::Get().IsLoaded(member->levelId)) ++owned;
			});
		return CountEntities() - owned;
	}


	/** 次の試験の前に、全 Level をアンロードして破棄を確定し、取り消したロードを外す */
	void ResetSpawnTestWorld()
	{
		LevelManager::Get().UnloadAll();
		LevelManager::Get().Tick(0.0f);   // 取り消した(done の)非同期ロードを外す
		aq::ecs::EntityContext::Get().FlushPendingCommands();
	}


	/** 数の比較の詳細(「名前 実際 / 期待」) */
	std::string CountDetail(const char* label, const uint32_t actual, const uint32_t expected)
	{
		return std::string(label) + " " + std::to_string(actual) + " / 期待 " + std::to_string(expected);
	}


	/** T8 (a): Load 直後は IsSpawned が false、Flush の後は true */
	void RunSpawnedTest(TestResults& results)
	{
		const std::string name = "T8 (a) IsSpawned";
		const std::string key  = "<t8-a>";
		if (!RegisterSpawnTestLevel(key, 3, 1)) {
			Report(results, name, false, "試験用 Level を登録できない");
			return;
		}

		LevelManager& levelManager = LevelManager::Get();
		const LevelId levelId      = levelManager.Load(key);
		const bool    beforeFlush  = levelManager.IsSpawned(levelId);
		aq::ecs::EntityContext::Get().FlushPendingCommands();
		const bool     afterFlush  = levelManager.IsSpawned(levelId);
		const uint32_t count       = CountEntities();

		const bool passed = levelId.IsValid() && !beforeFlush && afterFlush && count == 4;
		Report(results, name, passed, passed ? std::string()
			: "Load 直後 " + std::string(beforeFlush ? "true" : "false") + " / Flush 後 " + (afterFlush ? "true" : "false")
			  + " / " + CountDetail("エンティティ数", count, 4));

		ResetSpawnTestWorld();
		LevelRegistry::Get().Unregister(key);
	}


	/** T8 (b): Load → Unload → Flush でエンティティが 1 つも作られない(同期ロードの不具合) */
	void RunUnloadBeforeFlushTest(TestResults& results)
	{
		const std::string name = "T8 (b) Load → Unload → Flush";
		const std::string key  = "<t8-b>";
		if (!RegisterSpawnTestLevel(key, 3, 1)) {
			Report(results, name, false, "試験用 Level を登録できない");
			return;
		}

		LevelManager& levelManager = LevelManager::Get();
		const LevelId levelId      = levelManager.Load(key);
		levelManager.Unload(levelId);
		aq::ecs::EntityContext::Get().FlushPendingCommands();
		const uint32_t count = CountEntities();

		const bool passed = levelId.IsValid() && count == 0 && !levelManager.IsSpawned(levelId);
		Report(results, name, passed, passed ? std::string() : CountDetail("エンティティ数", count, 0));

		ResetSpawnTestWorld();
		LevelRegistry::Get().Unregister(key);
	}


	/** T8 (c): LoadAsync を 2 フレーム進めて根を Unload すると取り消され、以後エンティティが増えず、所属の無いものも残らない */
	void RunCancelRootAsyncTest(TestResults& results)
	{
		const std::string name = "T8 (c) LoadAsync 中に根を Unload";
		const std::string key  = "<t8-c>";
		if (!RegisterSpawnTestLevel(key, 4, 2)) {
			Report(results, name, false, "試験用 Level を登録できない");
			return;
		}

		LevelManager&           levelManager = LevelManager::Get();
		aq::ecs::EntityContext& ctx          = aq::ecs::EntityContext::Get();
		const LevelLoadHandle   handle       = levelManager.LoadAsync(key, LevelId(), T8_ASYNC_PER_FRAME);
		levelManager.Tick(0.0f);
		levelManager.Tick(0.0f);
		const uint32_t builtBeforeUnload = CountEntities();

		levelManager.Unload(handle.GetLevelId());
		const bool cancelled = handle.IsCancelled();
		const bool done      = handle.IsDone();
		ctx.FlushPendingCommands();
		const uint32_t afterUnload = CountEntities();

		for (int i = 0; i < T8_MAX_TICKS; ++i) {
			levelManager.Tick(0.0f);
		}
		ctx.FlushPendingCommands();
		const uint32_t afterTicks = CountEntities();
		const uint32_t orphans    = CountOrphanEntities();
		const size_t   active     = levelManager.GetActiveAsyncCount();

		const bool passed = handle.IsValid() && builtBeforeUnload == 2 && cancelled && done
			&& afterUnload == 0 && afterTicks == 0 && orphans == 0 && active == 0;
		Report(results, name, passed, passed ? std::string()
			: CountDetail("Unload 前", builtBeforeUnload, 2)
			  + " / IsCancelled " + (cancelled ? "true" : "false") + " / IsDone " + (done ? "true" : "false")
			  + " / " + CountDetail("Unload 後", afterUnload, 0) + " / " + CountDetail("Tick 後", afterTicks, 0)
			  + " / " + CountDetail("所属なし", orphans, 0) + " / 残りのロード " + std::to_string(active));

		ResetSpawnTestWorld();
		LevelRegistry::Get().Unregister(key);
	}


	/** T8 (d): LoadAsync 中に子のサブ Level だけを Unload すると、子は作られず、親は最後まで作られる */
	void RunCancelChildAsyncTest(TestResults& results)
	{
		const std::string name = "T8 (d) LoadAsync 中に子だけを Unload";
		const std::string key  = "<t8-d>";
		if (!RegisterSpawnTestLevel(key, 2, 2)) {
			Report(results, name, false, "試験用 Level を登録できない");
			return;
		}

		LevelManager&           levelManager = LevelManager::Get();
		aq::ecs::EntityContext& ctx          = aq::ecs::EntityContext::Get();
		const LevelLoadHandle   handle       = levelManager.LoadAsync(key, LevelId(), T8_ASYNC_PER_FRAME);
		const LevelId           rootId       = handle.GetLevelId();
		// インラインのサブ Level は "<inline>" で登録される。前の試験の Level は ResetSpawnTestWorld で外してあるので、これだけが当たる
		const LevelId           childId      = levelManager.Find("<inline>");

		// ジョブは親 E0, E1 → 子 C0, C1 の順。親を 1 つ作ったところで子だけを外す
		levelManager.Tick(0.0f);
		levelManager.Unload(childId);
		for (int i = 0; i < T8_MAX_TICKS && !handle.IsDone(); ++i) {
			levelManager.Tick(0.0f);
		}
		ctx.FlushPendingCommands();

		const uint32_t rootCount  = CountLevelEntities(rootId);
		const uint32_t childCount = CountLevelEntities(childId);
		const uint32_t total      = CountEntities();
		const bool     done       = handle.IsDone();
		const bool     cancelled  = handle.IsCancelled();

		const bool passed = handle.IsValid() && childId.IsValid() && rootCount == 2 && childCount == 0 && total == 2
			&& done && !cancelled && handle.Built() == handle.Total();
		Report(results, name, passed, passed ? std::string()
			: CountDetail("親", rootCount, 2) + " / " + CountDetail("子", childCount, 0) + " / " + CountDetail("全体", total, 2)
			  + " / IsDone " + (done ? "true" : "false") + " / IsCancelled " + (cancelled ? "true" : "false")
			  + " / built " + std::to_string(handle.Built()) + " / total " + std::to_string(handle.Total()));

		ResetSpawnTestWorld();
		LevelRegistry::Get().Unregister(key);
	}


	/** T8 (e): Register → Unregister → IsRegistered が false(Load は Debug で assert するので呼ばない) */
	void RunUnregisterTest(TestResults& results)
	{
		const std::string key = "<t8-e>";
		const bool registered = RegisterSpawnTestLevel(key, 1, 0);
		LevelRegistry::Get().Unregister(key);
		const bool afterUnregister = LevelRegistry::Get().IsRegistered(key);
		// 無いキーの Unregister は何もしない(落ちない)
		LevelRegistry::Get().Unregister("<t8-e-not-registered>");

		const bool passed = registered && !afterUnregister;
		Report(results, "T8 (e) Unregister", passed, passed ? std::string()
			: std::string("登録 ") + (registered ? "true" : "false") + " / 外した後 " + (afterUnregister ? "true" : "false"));
	}


#ifdef AQ_DEBUG_IMGUI
	/** T8 (f): プレビューの置き換えを 1 フレームに 10 回要求し、10 フレーム続けても、エンティティ数が変わらず登録が 1 つだけ */
	void RunPreviewReplaceTest(TestResults& results)
	{
		const std::string name = "T8 (f) プレビューの置き換え";

		aq::level::LevelEditModel model;
		if (!model.FromJson(MakeSpawnTestLevelJson("T8_Preview", 3, 0))) {
			Report(results, name, false, "編集モデルへ読み込めない");
			return;
		}

		aq::ecs::EntityContext&  ctx = aq::ecs::EntityContext::Get();
		std::vector<uint32_t>    counts;
		std::vector<std::string> keys;
		for (int frame = 0; frame < T8_PREVIEW_FRAMES; ++frame) {
			for (int i = 0; i < T8_PREVIEW_REQUESTS; ++i) {
				model.RequestPreview();
			}
			model.ProcessPreviewRequest();
			ctx.FlushPendingCommands();   // 次フレームの手順 3(5.3 節)の Flush の代わり
			counts.push_back(CountEntities());
			keys.push_back(model.GetPreviewKey());
		}

		// 要求が無いフレームでは何もしない
		const std::string lastKey = model.GetPreviewKey();
		model.ProcessPreviewRequest();
		const bool idleKept = model.GetPreviewKey() == lastKey;

		bool sameCount = true;
		for (const uint32_t count : counts) {
			if (count != counts.front()) sameCount = false;
		}
		uint32_t registeredCount = 0;
		for (const std::string& key : keys) {
			if (LevelRegistry::Get().IsRegistered(key)) ++registeredCount;
		}
		std::vector<std::string> uniqueKeys = keys;
		std::sort(uniqueKeys.begin(), uniqueKeys.end());
		uniqueKeys.erase(std::unique(uniqueKeys.begin(), uniqueKeys.end()), uniqueKeys.end());

		const bool spawned = LevelManager::Get().IsSpawned(model.GetPreviewLevelId());
		const bool passed  = counts.front() == 3 && sameCount && registeredCount == 1
			&& uniqueKeys.size() == static_cast<size_t>(T8_PREVIEW_FRAMES) && idleKept && spawned
			&& CountOrphanEntities() == 0;

		std::string countText;
		for (const uint32_t count : counts) {
			countText += (countText.empty() ? "" : ",") + std::to_string(count);
		}
		Report(results, name, passed, passed ? std::string()
			: "エンティティ数 [" + countText + "] / 期待 3 / 登録 " + std::to_string(registeredCount)
			  + " / 置き換え " + std::to_string(uniqueKeys.size()) + " 回 / IsSpawned " + (spawned ? "true" : "false")
			  + " / 要求なしで変わらない " + (idleKept ? "true" : "false"));

		// 後始末(プレビューの Level と登録を外す)
		LevelManager::Get().Unload(model.GetPreviewLevelId());
		ctx.FlushPendingCommands();
		LevelRegistry::Get().Unregister(model.GetPreviewKey());
		ResetSpawnTestWorld();
	}
#endif


	/**
	 * T8 生成と取り消し(7.8 節「9.」)
	 *
	 * 画面無しで Engine と EntityContext を作って行う。
	 * - Engine は Create だけで Initialize しない(ウィンドウ・デバイス・ThreadPool・AppHost を作らない)。
	 *   AppHost が無いので Engine::WaitForRenderIdle は何もしない(プレビューの置き換えが呼ぶ)。
	 * - EntityContext は System を登録せず Update も呼ばない。遅延生成は FlushPendingCommands で、
	 *   非同期ロードは LevelManager::Tick で進める(どちらも本来は System の反復の外で呼ぶもの)。
	 * - new / delete は MemoryManager を初期化しないので既定の HeapAllocator に落ちる(T1〜T7 と同じ)。
	 * - 後始末は 全 Level のアンロード → Tick → Flush → LevelRegistry::Clear → EntityContext::Finalize → Engine::Release の順。
	 */
	void RunSpawnTests(TestResults& results)
	{
		const bool engineCreatedHere = !aq::Engine::IsCreated();
		if (engineCreatedHere) aq::Engine::Create();
		aq::ecs::EntityContext::Initialize();

		RunSpawnedTest(results);
		RunUnloadBeforeFlushTest(results);
		RunCancelRootAsyncTest(results);
		RunCancelChildAsyncTest(results);
		RunUnregisterTest(results);
#ifdef AQ_DEBUG_IMGUI
		RunPreviewReplaceTest(results);
#else
		std::printf("        T8 (f): AQ_DEBUG_IMGUI が無い構成(Release)なので行わない(LevelEditModel が無い)\n");
#endif

		// 後始末
		ResetSpawnTestWorld();
		LevelRegistry::Get().Clear();
		aq::ecs::EntityContext::Finalize();
		if (engineCreatedHere) aq::Engine::Release();
	}


	/**
	 * T9 の試験用 System。Update が呼ばれた回数を数える。
	 * Id で型を分け、Mask が動くモードを宣言する(AddSystem は型で照合するので、別々の System になる)。
	 */
	template <int Id, aq::RunModeMask Mask>
	class RunModeCountSystem : public aq::ecs::SystemBase
	{
	public:
		void Update() override { ++callCount_; }
		aq::RunModeMask GetRunModes() const override { return Mask; }

		static std::atomic<int> callCount_;
	};

	template <int Id, aq::RunModeMask Mask>
	std::atomic<int> RunModeCountSystem<Id, Mask>::callCount_{ 0 };

	/** 宣言なし(既定 = Play のみ) / All / Edit | Play */
	using DefaultModeSystem  = RunModeCountSystem<0, aq::RunModeMask::Play>;
	using AllModeSystem      = RunModeCountSystem<1, aq::RunModeMask::All>;
	using EditPlayModeSystem = RunModeCountSystem<2, aq::RunModeMask::Edit | aq::RunModeMask::Play>;


	/** 既定を使う System(GetRunModes を上書きしない) */
	class UndeclaredModeSystem : public aq::ecs::SystemBase
	{
	public:
		void Update() override { ++callCount_; }

		static std::atomic<int> callCount_;
	};

	std::atomic<int> UndeclaredModeSystem::callCount_{ 0 };


	/** mode の名前 */
	const char* RunModeName(const aq::RunMode mode)
	{
		switch (mode) {
		case aq::RunMode::Edit:  return "Edit";
		case aq::RunMode::Play:  return "Play";
		case aq::RunMode::Pause: return "Pause";
		}
		return "?";
	}


	/**
	 * T9 実行モード(System の選択と、全モードでの Flush)
	 *
	 * T8 と同じく画面無しで Engine(Create のみ)と EntityContext を作る。T8 は System を登録しないので、
	 * T8 の後始末の後に EntityContext を作り直し、System を登録して FinalizeRegistration する。
	 * System は ThreadPool へ出されるので、ここだけ ThreadPool を作る。
	 * - 既定の System(GetRunModes を上書きしない)は Play のときだけ、All はすべて、Edit | Play は Pause 以外で呼ばれる。
	 * - EntityContext::Update(mode) の後の Flush は、どのモードでも行われる(積んだ生成が Update の後に済んでいる)。
	 */
	void RunRunModeTests(TestResults& results)
	{
		const bool engineCreatedHere = !aq::Engine::IsCreated();
		if (engineCreatedHere) aq::Engine::Create();
		aq::util::ThreadPool::Initialize(2);
		aq::ecs::EntityContext::Initialize();

		aq::ecs::EntityContext& ctx = aq::ecs::EntityContext::Get();
		ctx.AddSystem<UndeclaredModeSystem>();
		ctx.AddSystem<DefaultModeSystem>();
		ctx.AddSystem<AllModeSystem>();
		ctx.AddSystem<EditPlayModeSystem>();
		if (!ctx.FinalizeRegistration()) {
			Report(results, "T9 System の登録", false, "FinalizeRegistration に失敗");
		} else {
			struct Expectation
			{
				aq::RunMode mode;
				int         undeclared;
				int         playOnly;
				int         all;
				int         editPlay;
			};
			// 各モードで 1 回 Update したときに、System ごとに 1 回(動く)か 0 回(動かない)
			const Expectation expectations[] = {
				{ aq::RunMode::Edit,  0, 0, 1, 1 },
				{ aq::RunMode::Play,  1, 1, 1, 1 },
				{ aq::RunMode::Pause, 0, 0, 1, 0 },
			};

			for (const Expectation& expectation : expectations) {
				UndeclaredModeSystem::callCount_ = 0;
				DefaultModeSystem::callCount_    = 0;
				AllModeSystem::callCount_        = 0;
				EditPlayModeSystem::callCount_   = 0;

				const uint32_t before = CountEntities();
				ctx.RequestCreateEntity<>();
				const uint32_t queued = CountEntities();
				ctx.Update(expectation.mode);
				const uint32_t after = CountEntities();

				const std::string modeName = RunModeName(expectation.mode);
				const int undeclared = UndeclaredModeSystem::callCount_;
				const int playOnly   = DefaultModeSystem::callCount_;
				const int all        = AllModeSystem::callCount_;
				const int editPlay   = EditPlayModeSystem::callCount_;

				const bool systemsPassed = undeclared == expectation.undeclared && playOnly == expectation.playOnly
					&& all == expectation.all && editPlay == expectation.editPlay;
				Report(results, "T9 " + modeName + " の System", systemsPassed, systemsPassed ? std::string()
					: "未宣言 " + std::to_string(undeclared) + "(期待 " + std::to_string(expectation.undeclared) + ")"
					  + " / Play 宣言 " + std::to_string(playOnly) + "(期待 " + std::to_string(expectation.playOnly) + ")"
					  + " / All " + std::to_string(all) + "(期待 " + std::to_string(expectation.all) + ")"
					  + " / Edit|Play " + std::to_string(editPlay) + "(期待 " + std::to_string(expectation.editPlay) + ")");

				const bool flushPassed = queued == before && after == before + 1;
				Report(results, "T9 " + modeName + " の Flush", flushPassed, flushPassed ? std::string()
					: CountDetail("積んだ直後", queued, before) + " / " + CountDetail("Update 後", after, before + 1));
			}
		}

		// 後始末(System の登録は確定後に外せないので EntityContext ごと作り直す)
		aq::ecs::EntityContext::Finalize();
		aq::util::ThreadPool::Finalize();
		if (engineCreatedHere) aq::Engine::Release();
	}


#ifdef AQ_DEBUG_IMGUI
	using aq::ecs::FieldAttr;
	using aq::ecs::FieldEditEvent;
	using aq::ecs::FieldEditPhase;
	using aq::ecs::FieldItemRect;
	using aq::ecs::ImGuiEditEventVisitor;
	using aq::ecs::ImGuiFieldVisitor;

	/** 画面無しの ImGui の画面の大きさ */
	static constexpr float DISPLAY_WIDTH     = 640.0f;
	static constexpr float DISPLAY_HEIGHT    = 480.0f;
	/** 項目の左端からクリックする位置までの距離(ドラッグの部品・チェックボックスの箱の中に入る) */
	static constexpr float CLICK_OFFSET_X    = 8.0f;
	/** 試験の区切りで空けるフレーム数(1/60 秒 × 30 = 0.5 秒。ダブルクリックの間隔 0.3 秒より長い) */
	static constexpr int   RESET_IDLE_FRAMES = 30;


	/**
	 * T4 / T5 の試験用の型(float、片側だけ範囲付きの float、bool、Vector3、Quaternion、パス、文字列、readOnly、hidden)
	 */
	struct EditTestTarget
	{
		float                value   = 1.0f;
		float                clamped = 1.0f;
		bool                 flag    = false;
		aq::math::Vector3    vec     = aq::math::Vector3(1.0f, 2.0f, 3.0f);
		aq::math::Quaternion rot     = aq::math::Quaternion::Identity;
		std::string          path    = "a.tkm";
		std::string          text    = "hello";
		float                locked  = 5.0f;
		float                secret  = 7.0f;

		/** T4 (7): Vector3 を描く直前に「このフレームで既に別の項目が変わった」状態にする */
		bool                 markEditedBeforeVec = false;
		/** FieldPath が true を返した回数 */
		int                  pathCommitCount     = 0;

		template <typename V>
		void Reflect(V& visitor)
		{
			visitor.Field("value",   value,   "Value");
			visitor.Field("clamped", clamped, "Clamped", FieldAttr{}.Min(0.5f));
			visitor.Field("flag",    flag,    "Flag");
			if (markEditedBeforeVec) ImGui::GetCurrentContext()->AnyIdHasBeenEditedThisFrame = true;
			visitor.Field("vec",     vec,     "Vec");
			visitor.Field("rot",     rot,     "Rot");
			if (visitor.FieldPath("path", path, "Path", FieldAttr{}.Asset(aq::ecs::AssetKind::Model))) ++pathCommitCount;
			visitor.Field("text",    text,    "Text");
			visitor.Field("locked",  locked,  "Locked", FieldAttr{}.ReadOnly());
			visitor.Field("secret",  secret,  "Secret", FieldAttr{}.Hidden());
		}
	};


	/**
	 * 記録した編集イベント(フレーム番号付き)
	 */
	struct LoggedEvent
	{
		int            frame = 0;
		FieldEditEvent event;
	};


	const char* PhaseName(const FieldEditPhase phase)
	{
		switch (phase) {
		case FieldEditPhase::Begin:  return "Begin";
		case FieldEditPhase::Update: return "Update";
		case FieldEditPhase::Commit: return "Commit";
		case FieldEditPhase::Cancel: return "Cancel";
		}
		return "?";
	}


	/** イベント列を 1 行ずつ「f<フレーム> <段階> <キー> <値>」で出す */
	void PrintEvents(const char* title, const std::vector<LoggedEvent>& log)
	{
		std::printf("        %s: %zu 件\n", title, log.size());
		for (const LoggedEvent& logged : log) {
			const std::string value = logged.event.value.IsNull() ? std::string() : " " + StringifyOneLine(logged.event.value);
			std::printf("          f%d %s %s%s\n", logged.frame, PhaseName(logged.event.phase), logged.event.key.c_str(), value.c_str());
		}
	}


	/** キーごとに、Begin の後に Commit か Cancel が必ず 1 つ来るか(Begin の無い Update / Commit / Cancel も不可) */
	bool IsWellFormed(const std::vector<LoggedEvent>& log, std::string& why)
	{
		std::map<std::string, bool> open;
		for (const LoggedEvent& logged : log) {
			bool& isOpen = open[logged.event.key];
			const FieldEditPhase phase = logged.event.phase;
			if (phase == FieldEditPhase::Begin) {
				if (isOpen) { why = "閉じていない Begin の後に Begin: " + logged.event.key; return false; }
				isOpen = true;
			} else if (!isOpen) {
				why = std::string("Begin の無い ") + PhaseName(phase) + ": " + logged.event.key;
				return false;
			} else if (phase == FieldEditPhase::Commit || phase == FieldEditPhase::Cancel) {
				isOpen = false;
			}
		}
		for (const auto& [key, isOpen] : open) {
			if (isOpen) { why = "Commit / Cancel の来ない Begin: " + key; return false; }
		}
		return true;
	}


	size_t CountPhase(const std::vector<LoggedEvent>& log, const FieldEditPhase phase)
	{
		return static_cast<size_t>(std::count_if(log.begin(), log.end(), [phase](const LoggedEvent& logged)
			{
				return logged.event.phase == phase;
			}));
	}


	/** 段階の並び(Update は連続を 1 つに詰める)を "Begin Update Commit" のような文字列にする */
	std::string PhaseSequence(const std::vector<LoggedEvent>& log)
	{
		std::string out;
		FieldEditPhase previous = FieldEditPhase::Cancel;
		bool           hasPrevious = false;
		for (const LoggedEvent& logged : log) {
			const FieldEditPhase phase = logged.event.phase;
			if (hasPrevious && phase == FieldEditPhase::Update && previous == FieldEditPhase::Update) continue;
			out += (out.empty() ? "" : " ") + std::string(PhaseName(phase));
			previous    = phase;
			hasPrevious = true;
		}
		return out;
	}


	/**
	 * 画面を持たない ImGui のコンテキストで、入力を合成して試験用の型を描く
	 *
	 * 入力は 1 種類ずつ 1 フレームに入れる(ImGui の入力キューはフレームをまたいで細切れにするため、
	 * 同じフレームに押下と移動を混ぜない)。
	 */
	class HeadlessImGui
	{
	public:
		enum class VisitorKind : uint8_t { EditEvent, Field };

	private:
		ImGuiContext*              context_     = nullptr;
		int                        frame_       = 0;
		ImVec2                     mouse_       = ImVec2(-FLT_MAX, -FLT_MAX);


	public:
		EditTestTarget             target;
		VisitorKind                visitorKind  = VisitorKind::EditEvent;
		/** false の間は試験用の型を描かない(ウィンドウは描く) */
		bool                       drawFields   = true;
		std::vector<LoggedEvent>   log;
		std::vector<FieldItemRect> itemRects;


	public:
		HeadlessImGui()
		{
			context_ = ImGui::CreateContext();
			ImGui::SetCurrentContext(context_);
			ImGuiIO& io = ImGui::GetIO();
			io.DisplaySize           = ImVec2(DISPLAY_WIDTH, DISPLAY_HEIGHT);
			io.DeltaTime             = 1.0f / 60.0f;
			io.IniFilename           = nullptr;
			io.LogFilename           = nullptr;
			// Ctrl を文字どおり Ctrl として扱う(Mac の既定の Cmd との入れ替えをしない)
			io.ConfigMacOSXBehaviors = false;

			// レンダラーが無いので、フォントアトラスを旧来の経路で自分で作る
			// (RendererHasTextures を立てないバックエンドは NewFrame の前にアトラスを作っておく必要がある)
			unsigned char* pixels = nullptr;
			int            width  = 0;
			int            height = 0;
			io.Fonts->GetTexDataAsAlpha8(&pixels, &width, &height);
		}

		~HeadlessImGui()
		{
			ImGui::DestroyContext(context_);
		}

		HeadlessImGui(const HeadlessImGui&)            = delete;
		HeadlessImGui& operator=(const HeadlessImGui&) = delete;


	public:
		/** 1 フレーム進める(入力は呼ぶ前にキューへ入れておく) */
		void Frame()
		{
			++frame_;
			ImGui::NewFrame();
			ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
			ImGui::SetNextWindowSize(ImVec2(DISPLAY_WIDTH, DISPLAY_HEIGHT));
			ImGui::Begin("ReflectionTest", nullptr,
				ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
			if (drawFields) {
				if (visitorKind == VisitorKind::EditEvent) {
					ImGuiEditEventVisitor visitor;
					visitor.recordItemRects = true;
					target.Reflect(visitor);
					for (FieldEditEvent& event : visitor.events) {
						log.push_back(LoggedEvent{ frame_, std::move(event) });
					}
					itemRects = std::move(visitor.itemRects);
				} else {
					ImGuiFieldVisitor visitor;
					target.Reflect(visitor);
				}
			}
			ImGui::End();
			ImGui::Render();
		}

		void Idle(const int count)
		{
			for (int i = 0; i < count; ++i) Frame();
		}

		void MoveTo(const float x, const float y)
		{
			mouse_ = ImVec2(x, y);
			ImGui::GetIO().AddMousePosEvent(x, y);
			Frame();
		}

		/** 1 フレームに dx ずつ count 回動かす */
		void MoveBy(const float dx, const int count)
		{
			for (int i = 0; i < count; ++i) MoveTo(mouse_.x + dx, mouse_.y);
		}

		void Button(const bool down)
		{
			ImGui::GetIO().AddMouseButtonEvent(0, down);
			Frame();
		}

		void Key(const ImGuiKey key, const bool down)
		{
			ImGui::GetIO().AddKeyEvent(key, down);
			Frame();
		}

		void Ctrl(const bool down)
		{
			ImGuiIO& io = ImGui::GetIO();
			io.AddKeyEvent(ImGuiKey_LeftCtrl, down);
			io.AddKeyEvent(ImGuiMod_Ctrl, down);
			Frame();
		}

		void TapKey(const ImGuiKey key)
		{
			Key(key, true);
			Key(key, false);
		}

		void Type(const char* text)
		{
			for (const char* c = text; *c; ++c) {
				ImGui::GetIO().AddInputCharacter(static_cast<unsigned int>(*c));
			}
			Frame();
		}

		/** 項目の矩形(直前に描いたフレームのもの)。無ければ false */
		bool FindRect(const char* key, FieldItemRect& out) const
		{
			for (const FieldItemRect& rect : itemRects) {
				if (rect.key == key) {
					out = rect;
					return true;
				}
			}
			return false;
		}

		/** 項目の左端寄り(1 つ目の部品・チェックボックスの箱)へマウスを動かす */
		bool MoveToItem(const char* key)
		{
			FieldItemRect rect;
			if (!FindRect(key, rect)) return false;
			MoveTo(rect.min.x + CLICK_OFFSET_X, (rect.min.y + rect.max.y) * 0.5f);
			return true;
		}

		/** 項目をクリックする(押して離す) */
		bool Click(const char* key)
		{
			if (!MoveToItem(key)) return false;
			Button(true);
			Button(false);
			return true;
		}

		/** 項目をドラッグする(押して dx ずつ count フレーム動かして離す) */
		bool Drag(const char* key, const float dx, const int count)
		{
			if (!MoveToItem(key)) return false;
			Button(true);
			MoveBy(dx, count);
			Button(false);
			return true;
		}

		/** Ctrl+クリックで直接入力にし、文字を打つ(確定はしない) */
		bool CtrlClickAndType(const char* key, const char* text)
		{
			if (!MoveToItem(key)) return false;
			Ctrl(true);
			Button(true);
			Button(false);
			Ctrl(false);   // Ctrl を押したままだと文字入力が無視される
			Type(text);
			return true;
		}

		/** 文字列の項目をクリックし、中身を消して text を打つ(確定はしない) */
		bool ReplaceText(const char* key, const char* text, const size_t currentLength)
		{
			if (!Click(key)) return false;
			TapKey(ImGuiKey_End);
			for (size_t i = 0; i < currentLength; ++i) TapKey(ImGuiKey_Backspace);
			Type(text);
			return true;
		}

		/** 試験の区切り: 入力を離し、マウスを外へ出し、記録を消す */
		void Reset()
		{
			ImGuiIO& io = ImGui::GetIO();
			io.AddMouseButtonEvent(0, false);
			Frame();
			io.AddKeyEvent(ImGuiKey_LeftCtrl, false);
			io.AddKeyEvent(ImGuiMod_Ctrl, false);
			Frame();
			MoveTo(DISPLAY_WIDTH - 4.0f, DISPLAY_HEIGHT - 4.0f);
			drawFields                 = true;
			visitorKind                = VisitorKind::EditEvent;
			target.markEditedBeforeVec = false;
			// 前の試験のクリックと同じ位置を続けてクリックするとダブルクリック(= 直接入力)になるので、
			// ダブルクリックの間隔(io.MouseDoubleClickTime)より長く空ける
			Idle(RESET_IDLE_FRAMES);
			log.clear();
		}
	};


	/** (1)〜(7) の共通の確かめ: 形が整っていること */
	bool CheckWellFormed(const std::vector<LoggedEvent>& log, std::string& detail)
	{
		std::string why;
		if (IsWellFormed(log, why)) return true;
		detail = why;
		return false;
	}


	/** T4 編集イベント */
	void RunEditEventTests(TestResults& results, HeadlessImGui& gui)
	{
		// 項目の矩形を取る
		gui.Reset();

		// (1) float のドラッグ
		{
			gui.Reset();
			std::string detail;
			bool passed = gui.Drag("value", 10.0f, 3);
			const std::vector<LoggedEvent> events = gui.log;
			PrintEvents("T4 (1) float のドラッグ", events);
			passed = passed && CheckWellFormed(events, detail);
			if (passed) {
				passed = events.size() >= 3
					&& events.front().event.phase == FieldEditPhase::Begin
					&& events.back().event.phase == FieldEditPhase::Commit
					&& CountPhase(events, FieldEditPhase::Begin) == 1
					&& CountPhase(events, FieldEditPhase::Update) >= 1
					&& CountPhase(events, FieldEditPhase::Commit) == 1
					&& CountPhase(events, FieldEditPhase::Cancel) == 0
					&& events.front().event.value.AsFloat() == 1.0f
					&& events.back().event.value.AsFloat() == gui.target.value
					&& gui.target.value != 1.0f;
				if (!passed) detail = "列: " + PhaseSequence(events);
			}
			Report(results, "T4 (1) float のドラッグ", passed, detail);
		}

		// (2) 押して動かさずに離す
		{
			gui.Reset();
			std::string detail;
			bool passed = gui.Click("value");
			const std::vector<LoggedEvent> events = gui.log;
			PrintEvents("T4 (2) 押して動かさずに離す", events);
			passed = passed && CheckWellFormed(events, detail);
			if (passed) {
				passed = PhaseSequence(events) == "Begin Cancel";
				if (!passed) detail = "列: " + PhaseSequence(events);
			}
			Report(results, "T4 (2) 押して動かさずに離す", passed, detail);
		}

		// (3) チェックボックスのクリック
		{
			gui.Reset();
			std::string detail;
			const bool before = gui.target.flag;
			bool passed = gui.Click("flag");
			const std::vector<LoggedEvent> events = gui.log;
			PrintEvents("T4 (3) チェックボックス", events);
			passed = passed && CheckWellFormed(events, detail);
			if (passed) {
				passed = PhaseSequence(events) == "Begin Update Commit"
					&& gui.target.flag != before
					&& events.back().event.value.AsBool() == gui.target.flag;
				if (!passed) detail = "列: " + PhaseSequence(events);
			}
			Report(results, "T4 (3) チェックボックス", passed, detail);
		}

		// (4) Vector3 の 1 成分のドラッグ
		{
			gui.Reset();
			std::string detail;
			bool passed = gui.Drag("vec", 10.0f, 3);
			const std::vector<LoggedEvent> events = gui.log;
			PrintEvents("T4 (4) Vector3 の 1 成分のドラッグ", events);
			passed = passed && CheckWellFormed(events, detail);
			if (passed) {
				passed = PhaseSequence(events) == "Begin Update Commit"
					&& gui.target.vec.x != 1.0f
					&& gui.target.vec.y == 2.0f
					&& gui.target.vec.z == 3.0f;
				if (!passed) detail = "列: " + PhaseSequence(events);
			}
			Report(results, "T4 (4) Vector3 の 1 成分のドラッグ", passed, detail);
		}

		// (5) パスと文字列: 値を変えて Enter で Begin と Commit が同じフレームに 1 組、同じ値の Enter では何も出ない
		{
			struct TextCase { const char* key; const char* newText; std::string* value; };
			const TextCase cases[] = {
				{ "path", "b.tkm", &gui.target.path },
				{ "text", "world", &gui.target.text },
			};
			for (const TextCase& textCase : cases) {
				const std::string name = std::string("T4 (5) ") + textCase.key;

				// 値を変えて Enter
				gui.Reset();
				const std::string before     = *textCase.value;
				const int         pathBefore = gui.target.pathCommitCount;
				bool passed = gui.ReplaceText(textCase.key, textCase.newText, before.size());
				gui.TapKey(ImGuiKey_Enter);
				const std::vector<LoggedEvent> changed = gui.log;
				PrintEvents((name + " 値を変えて Enter").c_str(), changed);
				std::string detail;
				passed = passed && CheckWellFormed(changed, detail);
				if (passed) {
					passed = changed.size() == 2
						&& changed[0].event.phase == FieldEditPhase::Begin
						&& changed[1].event.phase == FieldEditPhase::Commit
						&& changed[0].frame == changed[1].frame
						&& changed[0].event.value.AsString() == before
						&& changed[1].event.value.AsString() == textCase.newText
						&& *textCase.value == textCase.newText;
					if (!passed) detail = "列: " + PhaseSequence(changed);
				}

				// 同じ値で Enter
				gui.Reset();
				const int pathAfterChange = gui.target.pathCommitCount;
				passed = gui.Click(textCase.key) && passed;
				gui.TapKey(ImGuiKey_Enter);
				const std::vector<LoggedEvent> same = gui.log;
				PrintEvents((name + " 同じ値で Enter").c_str(), same);
				if (passed && !same.empty()) {
					passed = false;
					detail = "同じ値の Enter でイベントが出た: " + PhaseSequence(same);
				}

				// FieldPath の戻り値は「確定し、かつ値が変わった」ときだけ true
				if (passed && std::string_view(textCase.key) == "path") {
					passed = (pathAfterChange == pathBefore + 1) && (gui.target.pathCommitCount == pathAfterChange);
					if (!passed) detail = "FieldPath の戻り値が true になった回数が違う";
				}
				Report(results, name, passed, detail);
			}
		}

		// (6) readOnly と hidden
		{
			gui.Reset();
			std::string detail;
			const float lockedBefore = gui.target.locked;
			bool passed = gui.Drag("locked", 10.0f, 3);
			gui.Click("locked");
			const std::vector<LoggedEvent> events = gui.log;
			PrintEvents("T4 (6) readOnly のドラッグとクリック", events);
			FieldItemRect hiddenRect;
			const bool hasHiddenRect = gui.FindRect("secret", hiddenRect);
			passed = passed && events.empty() && gui.target.locked == lockedBefore && !hasHiddenRect;
			if (!passed) {
				detail = hasHiddenRect ? "hidden が itemRects に出た" : "readOnly でイベントが出たか値が変わった";
			}
			Report(results, "T4 (6) readOnly と hidden", passed, detail);
		}

		// (7) 同じフレームで先に別の項目が変わっていても、Vector3 のドラッグに Update が出る
		{
			gui.Reset();
			gui.target.markEditedBeforeVec = true;
			std::string detail;
			bool passed = gui.Drag("vec", 10.0f, 3);
			const std::vector<LoggedEvent> events = gui.log;
			PrintEvents("T4 (7) 先に別の項目が変わったフレームの Vector3", events);
			passed = passed && CheckWellFormed(events, detail);
			if (passed) {
				passed = PhaseSequence(events) == "Begin Update Commit";
				if (!passed) detail = "列: " + PhaseSequence(events);
			}
			Report(results, "T4 (7) 先に別の項目が変わったフレームの Vector3", passed, detail);
		}

		// (8) ドラッグの途中で描くのをやめ、再び描いても、古い操作の Commit / Cancel が出ない
		{
			gui.Reset();
			bool passed = gui.MoveToItem("value");
			gui.Button(true);
			gui.MoveBy(10.0f, 2);
			const size_t beforeHide = gui.log.size();
			gui.drawFields = false;
			gui.Idle(2);
			gui.drawFields = true;
			gui.MoveBy(10.0f, 2);
			gui.Button(false);
			gui.Idle(2);
			const std::vector<LoggedEvent> events = gui.log;
			PrintEvents("T4 (8) ドラッグの途中で描くのをやめる", events);
			const std::vector<LoggedEvent> afterHide(events.begin() + static_cast<std::ptrdiff_t>(std::min(beforeHide, events.size())), events.end());
			passed = passed
				&& PhaseSequence(std::vector<LoggedEvent>(events.begin(), events.begin() + static_cast<std::ptrdiff_t>(std::min(beforeHide, events.size())))) == "Begin Update"
				&& afterHide.empty();
			Report(results, "T4 (8) ドラッグの途中で描くのをやめる", passed,
				passed ? std::string() : "列: " + PhaseSequence(events));
		}
		{
			gui.Reset();
			bool passed = gui.CtrlClickAndType("value", "3");
			const size_t beforeHide = gui.log.size();
			gui.drawFields = false;
			gui.Idle(2);
			gui.drawFields = true;
			gui.Idle(3);
			const std::vector<LoggedEvent> events = gui.log;
			PrintEvents("T4 (8) 直接入力の途中で描くのをやめる", events);
			const std::vector<LoggedEvent> afterHide(events.begin() + static_cast<std::ptrdiff_t>(std::min(beforeHide, events.size())), events.end());
			// 古い操作の Commit / Cancel が出ず、Begin の無い Update も出ない。出るなら同じフレームの Begin と Commit の 1 組
			bool afterOk = afterHide.empty();
			if (!afterOk && afterHide.size() == 2) {
				afterOk = afterHide[0].event.phase == FieldEditPhase::Begin
					&& afterHide[1].event.phase == FieldEditPhase::Commit
					&& afterHide[0].frame == afterHide[1].frame;
			}
			passed = passed && afterOk;
			Report(results, "T4 (8) 直接入力の途中で描くのをやめる", passed,
				passed ? std::string() : "列: " + PhaseSequence(events));
		}

		// 記録だけ: DragFloat3 で部品を Tab で移ったときの列(合否に使わない。設計 6.4 節 3)
		{
			gui.Reset();
			gui.CtrlClickAndType("vec", "4");
			gui.TapKey(ImGuiKey_Tab);
			gui.Type("5");
			gui.TapKey(ImGuiKey_Enter);
			gui.Idle(2);
			PrintEvents("T4 記録 DragFloat3 の Tab 移動", gui.log);
			std::printf("          vec = (%.3f, %.3f, %.3f)\n", gui.target.vec.x, gui.target.vec.y, gui.target.vec.z);
		}

		gui.Reset();
	}


	/** T5 属性: 片側だけ範囲付きの float は下限に収まり、範囲の無い float は制限されない */
	void RunAttributeTests(TestResults& results, HeadlessImGui& gui)
	{
		// 項目の矩形は編集イベント Visitor で取る(2 つの Visitor は同じ配置で描く)
		gui.Reset();

		struct VisitorCase { HeadlessImGui::VisitorKind kind; const char* name; };
		const VisitorCase visitors[] = {
			{ HeadlessImGui::VisitorKind::Field,     "ImGuiFieldVisitor" },
			{ HeadlessImGui::VisitorKind::EditEvent, "ImGuiEditEventVisitor" },
		};
		for (const VisitorCase& visitorCase : visitors) {
			const std::string prefix = std::string("T5 ") + visitorCase.name + " ";

			// 下限付き: ドラッグで下限の外へ
			{
				gui.Reset();
				gui.visitorKind    = visitorCase.kind;
				gui.target.clamped = 1.0f;
				const bool moved = gui.Drag("clamped", -20.0f, 10);
				const bool passed = moved && gui.target.clamped == 0.5f;
				char detail[64];
				std::snprintf(detail, sizeof(detail), "値 %.3f", gui.target.clamped);
				Report(results, prefix + "下限付き ドラッグ", passed, passed ? std::string() : detail);
			}
			// 下限付き: Ctrl+クリックで下限の外の値を入力
			{
				gui.Reset();
				gui.visitorKind    = visitorCase.kind;
				gui.target.clamped = 1.0f;
				const bool typed = gui.CtrlClickAndType("clamped", "-5");
				gui.TapKey(ImGuiKey_Enter);
				const bool passed = typed && gui.target.clamped == 0.5f;
				char detail[64];
				std::snprintf(detail, sizeof(detail), "値 %.3f", gui.target.clamped);
				Report(results, prefix + "下限付き 直接入力", passed, passed ? std::string() : detail);
			}
			// 範囲なし: ドラッグは制限されない
			{
				gui.Reset();
				gui.visitorKind  = visitorCase.kind;
				gui.target.value = 1.0f;
				const bool moved = gui.Drag("value", -20.0f, 10);
				const bool passed = moved && gui.target.value < 0.0f;
				char detail[64];
				std::snprintf(detail, sizeof(detail), "値 %.3f", gui.target.value);
				Report(results, prefix + "範囲なし ドラッグ", passed, passed ? std::string() : detail);
			}
			// 範囲なし: 直接入力も制限されない
			{
				gui.Reset();
				gui.visitorKind  = visitorCase.kind;
				gui.target.value = 1.0f;
				const bool typed = gui.CtrlClickAndType("value", "-5");
				gui.TapKey(ImGuiKey_Enter);
				const bool passed = typed && gui.target.value == -5.0f;
				char detail[64];
				std::snprintf(detail, sizeof(detail), "値 %.3f", gui.target.value);
				Report(results, prefix + "範囲なし 直接入力", passed, passed ? std::string() : detail);
			}
		}
		gui.Reset();
	}


	/** T4 / T5 を画面無しの ImGui で回す */
	void RunImGuiTests(TestResults& results)
	{
		HeadlessImGui gui;
		RunEditEventTests(results, gui);
		RunAttributeTests(results, gui);
	}
#endif


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

#ifdef AQ_DEBUG_IMGUI
		RunImGuiTests(results);
#else
		std::printf("        T4 / T5: AQ_DEBUG_IMGUI が無い構成(Release)なので行わない\n");
#endif

#ifdef AQ_DEBUG_IMGUI
		RunNodeRoundTripTests(results, options);
#else
		std::printf("        T6: AQ_DEBUG_IMGUI が無い構成(Release)なので行わない(編集ノードが無い)\n");
#endif

		RunNumberFormatTests(results);
#ifdef AQ_DEBUG_IMGUI
		RunNumberFileTests(results);
#else
		std::printf("        T7 ファイル往復: AQ_DEBUG_IMGUI が無い構成(Release)なので行わない(編集ノードが無い)\n");
#endif

		RunSpawnTests(results);
		RunRunModeTests(results);

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
