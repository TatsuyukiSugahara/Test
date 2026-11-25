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
// E7a で T10 構造操作(LevelEditModel のノードの挿入・取り外し・移動・名前・相対参照の判定)を足した。画面も Engine も作らない(Debug 構成のみ)。
// E7b で T11 値の操作(LevelEditModel のコンポーネントの値・追加・削除、参照ノードの解決と基準の付け替え)を足した。(a)〜(f)と(h)は画面も Engine も作らず TestData を読み、
// (g)(保存した Level を生成して実体の値を確かめる)は T8 と同じ Engine + EntityContext の環境で行う(Debug 構成のみ)。
// T6 と T7 のファイル往復も、編集ノード(PrefabEditNode)が AQ_DEBUG_IMGUI のときだけあるので Debug 構成のみ。
// E7 の後で T12 先頭コメントの保持(ExtractLeadingComments と、LevelEditModel の保存での書き戻し)を足した。(a) は全構成、(b) は Debug のみ。
// E8a で T13 Undo / Redo(Editor のコマンドの取り消しとやり直し、CommandHistory の列と保存の印)を足した。Editor の Commands/*.cpp と
// CommandHistory.cpp を直接コンパイルして使い、画面も Engine も作らない(Debug 構成のみ)。
// E8b で T14 ログの通知(aq::debug::SetLogListener と、Editor の ConsoleLog の行の分割・重要度・上限・複数スレッド)を足した。
// Editor の ConsoleLog.cpp を直接コンパイルして使い、画面も Engine も作らない(全構成)。
// E9a で T15 (a) アセットの一覧(Editor の AssetCatalog の ClassifyAsset と、TestData/T15_Assets の木の読み込み)を足した。
// Editor の AssetCatalog.cpp を直接コンパイルして使い、画面も Engine も作らない(全構成)。
#include "aq.h"
#include "ECS/ComponentRegistry.h"
#include "ECS/AlignedStorage.h"
#include "Level/LevelComponentRegistry.h"
#include "Level/LevelComponents.h"
#include "Level/LevelManager.h"
#include "Level/LevelRegistry.h"
#include "Level/LevelSerializer.h"
#include "Util/SimpleJson.h"
#include "ConsoleLog.h"                    // T14(Editor のソースを全構成で直接コンパイルする)
#include "AssetCatalog.h"                  // T15(Editor のソースを全構成で直接コンパイルする)
#ifdef AQ_DEBUG_IMGUI
#include "Level/LevelEditModel.h"
#include "ECS/PrefabEditor.h"
#include "ECS/PrefabEditNodeOps.h"
#include "ECS/ImGuiFieldVisitor.h"
#include "ECS/ImGuiEditEventVisitor.h"
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>   // T4 (7) で ImGuiContext::AnyIdHasBeenEditedThisFrame を直接立てる
#include "CommandHistory.h"                // T13(Editor のソースを Debug だけ直接コンパイルする)
#include "Commands/ComponentCommands.h"
#include "Commands/NodeCommands.h"
#endif

#include <array>
#include <atomic>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
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

	/** T15 の試験データのフォルダ名(TEST_DATA_DIR の下) */
	static constexpr const char* T15_ASSETS_DIR = "T15_Assets";


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
			"      T9 実行モード(Edit / Play / Pause で動く System と、全モードでの Flush)を試す。\n"
			"      Debug 構成では T10 構造操作(LevelEditModel のノードの挿入・取り外し・移動・名前・相対参照の判定)も行う。\n"
			"      Debug 構成では T11 値の操作(LevelEditModel のコンポーネントの値・追加・削除、参照ノードの解決と基準の付け替え、保存した Level の生成)も行う。\n"
			"      Debug 構成では T13 Undo / Redo(Editor のコマンドの取り消しとやり直し、CommandHistory)も行う。\n"
			"      T14 ログの通知(SetLogListener、Editor の ConsoleLog の行の分割・重要度・上限・複数スレッド)を試す。\n"
			"      T15 (a) アセットの一覧(Editor の AssetCatalog の ClassifyAsset と、TestData/T15_Assets の木の読み込み)を試す。\n");
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
		for (fs::recursive_directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
			const fs::directory_entry& entry = *it;
			// T15 の試験データ(中身が空に近い木)は T6 の往復の対象にしない
			if (entry.is_directory() && entry.path().filename() == T15_ASSETS_DIR) {
				it.disable_recursion_pending();
				continue;
			}
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


#ifdef AQ_DEBUG_IMGUI
	/**
	 * T10 の試験用 Level
	 *
	 * 実体ノード A(子 A1(孫 A1a)、A2)・B、参照ノード Ref(相対パス)、インラインの subLevel(S1)、
	 * ファイル参照の subLevel(相対パス)を持つ。
	 */
	static constexpr const char* STRUCTURE_TEST_LEVEL_JSON = R"({
  "name": "T10",
  "entities": [
    { "name": "A", "components": { "Transform": {} }, "children": [
      { "name": "A1", "components": { "Transform": {} }, "children": [
        { "name": "A1a", "components": { "Transform": {} } } ] },
      { "name": "A2", "components": { "Transform": {} } } ] },
    { "name": "B", "components": { "Transform": {} } },
    { "name": "Ref", "prefab": "T6_Base.prefab.json" }
  ],
  "subLevels": [
    { "loadOnStart": true, "level": { "name": "Inline", "entities": [
      { "name": "S1", "components": { "Transform": {} } } ] } },
    { "loadOnStart": true, "level": "Other.level.json" }
  ]
})";

	/** 1 段目のインライン subLevel(Inline)と、ファイル参照の subLevel の添字 */
	static constexpr int STRUCTURE_INLINE_SUB = 0;
	static constexpr int STRUCTURE_FILE_SUB   = 1;

	/** 位置を指定しない(末尾) */
	static constexpr size_t STRUCTURE_APPEND  = aq::level::APPEND;


	/** ノードの並びを「A(A1(A1a),A2),B」の形の文字にする */
	std::string NodeOutline(const JsonValue& nodes)
	{
		std::string text;
		if (!nodes.IsArray()) return text;
		for (const JsonValue& node : nodes.GetArray()) {
			if (!text.empty()) text += ",";
			text += node["name"].AsString();
			if (node.Contains("children") && node["children"].IsArray() && node["children"].Size() > 0) {
				text += "(" + NodeOutline(node["children"]) + ")";
			}
		}
		return text;
	}


	/** Level 全体を「entities|[Inline:S1][file:パス]」の形の文字にする(1 段目の subLevel だけ) */
	std::string LevelOutline(const JsonValue& level)
	{
		std::string text = level.Contains("entities") ? NodeOutline(level["entities"]) : std::string();
		text += "|";
		if (level.Contains("subLevels") && level["subLevels"].IsArray()) {
			for (const JsonValue& sub : level["subLevels"].GetArray()) {
				const JsonValue& inner = sub["level"];
				if (inner.IsObject()) {
					text += "[" + inner["name"].AsString() + ":" + NodeOutline(inner["entities"]) + "]";
				} else {
					text += "[file:" + inner.AsString() + "]";
				}
			}
		}
		return text;
	}


	/** name のノードを nodes 以下から探す */
	const PrefabEditNode* FindNodeByName(const std::vector<std::unique_ptr<PrefabEditNode>>& nodes, const std::string& name)
	{
		for (const auto& node : nodes) {
			if (node->name == name) return node.get();
			if (const PrefabEditNode* found = FindNodeByName(node->children, name)) return found;
		}
		return nullptr;
	}


	/** モデルの中から name のノードの ID を探す(トップレベルと 1 段目のインライン subLevel)。無ければ 0 */
	uint32_t IdOf(const aq::level::LevelEditModel& model, const std::string& name)
	{
		if (const PrefabEditNode* node = FindNodeByName(model.GetEntities(), name)) return node->editId;
		for (const aq::level::SubLevelEdit& sub : model.GetSubLevels()) {
			if (!sub.isInline) continue;
			if (const PrefabEditNode* node = FindNodeByName(sub.entities, name)) return node->editId;
		}
		return 0;
	}


	/** Transform 付きの新しい実体ノード */
	std::unique_ptr<PrefabEditNode> MakeEntityNode(const char* name)
	{
		auto node  = std::make_unique<PrefabEditNode>();
		node->name = name;
		aq::ecs::PrefabNodeEnsureTransform(*node);
		return node;
	}


	/** T10 の試験用 Level を読み込む。読めなければ false */
	bool LoadStructureTestModel(aq::level::LevelEditModel& model)
	{
		return model.FromJson(aq::util::JsonParser::ParseString(STRUCTURE_TEST_LEVEL_JSON));
	}


	/** 並びが期待どおりかを 1 件として出す(違えば、期待と実際を説明に出す) */
	void ReportOutline(TestResults& results, const std::string& name, const aq::level::LevelEditModel& model, const std::string& expected)
	{
		const std::string actual = LevelOutline(model.ToJson());
		Report(results, name, actual == expected, actual == expected ? std::string() : "期待 " + expected + " / 実際 " + actual);
	}


	/** T10 (a): InsertNode(トップ・子・subLevel のルート)で ID が振られ、ToJson の位置に出る。失敗する挿入はモデルを変えない */
	void RunStructureInsertTest(TestResults& results)
	{
		const std::string name = "T10 (a) InsertNode";
		aq::level::LevelEditModel model;
		if (!LoadStructureTestModel(model)) { Report(results, name, false, "試験用 Level を読み込めない"); return; }

		// トップレベルの 1 番目(B の前)。子を持つノードは、子孫にも ID が振られる
		auto top = MakeEntityNode("N1");
		top->children.push_back(MakeEntityNode("N1c"));
		const uint32_t topId      = model.InsertNode(aq::level::NodeParent{ 0, -1 }, 1, std::move(top));
		const uint32_t topChildId = IdOf(model, "N1c");
		ReportOutline(results, name + " トップ", model, "A(A1(A1a),A2),N1(N1c),B,Ref|[Inline:S1][file:Other.level.json]");

		// 子(A1 の末尾)
		const uint32_t childId = model.InsertNode(aq::level::NodeParent{ IdOf(model, "A1"), -1 }, STRUCTURE_APPEND, MakeEntityNode("N2"));
		ReportOutline(results, name + " 子", model, "A(A1(A1a,N2),A2),N1(N1c),B,Ref|[Inline:S1][file:Other.level.json]");

		// subLevel のルート(先頭)
		const uint32_t subId = model.InsertNode(aq::level::NodeParent{ 0, STRUCTURE_INLINE_SUB }, 0, MakeEntityNode("N3"));
		ReportOutline(results, name + " subLevel", model, "A(A1(A1a,N2),A2),N1(N1c),B,Ref|[Inline:N3,S1][file:Other.level.json]");

		// 範囲外の index は末尾
		const uint32_t endId = model.InsertNode(aq::level::NodeParent{ 0, -1 }, 99, MakeEntityNode("N4"));
		ReportOutline(results, name + " 範囲外は末尾", model, "A(A1(A1a,N2),A2),N1(N1c),B,Ref,N4|[Inline:N3,S1][file:Other.level.json]");

		// ID は 0 でなく、すべて違い、FindNode で引ける
		const uint32_t ids[] = { topId, topChildId, childId, subId, endId };
		bool idsOk = true;
		for (size_t i = 0; i < std::size(ids); ++i) {
			if (ids[i] == 0 || model.FindNode(ids[i]) == nullptr) idsOk = false;
			for (size_t j = i + 1; j < std::size(ids); ++j) {
				if (ids[i] == ids[j]) idsOk = false;
			}
		}
		Report(results, name + " ID", idsOk, idsOk ? std::string() : "ID が 0・重複・引けない");

		// 採番済みの ID は変えない(取り外した A を同じ ID のまま戻す)
		const uint32_t idA = IdOf(model, "A");
		auto detached = model.DetachNode(idA);
		const uint32_t reinserted = detached ? model.InsertNode(aq::level::NodeParent{ 0, -1 }, 0, std::move(detached)) : 0;
		Report(results, name + " 採番済みは変えない", reinserted == idA && IdOf(model, "A") == idA,
			"元 " + std::to_string(idA) + " / 戻した後 " + std::to_string(reinserted));

		// 失敗する挿入は 0 を返し、モデルを変えない
		const JsonValue before = model.ToJson();
		const uint32_t failRef  = model.InsertNode(aq::level::NodeParent{ IdOf(model, "Ref"), -1 }, 0, MakeEntityNode("X"));
		const uint32_t failFile = model.InsertNode(aq::level::NodeParent{ 0, STRUCTURE_FILE_SUB }, 0, MakeEntityNode("X"));
		const uint32_t failSub  = model.InsertNode(aq::level::NodeParent{ 0, 9 }, 0, MakeEntityNode("X"));
		const uint32_t failNone = model.InsertNode(aq::level::NodeParent{ 99999, -1 }, 0, MakeEntityNode("X"));
		const uint32_t failNull = model.InsertNode(aq::level::NodeParent{ 0, -1 }, 0, nullptr);
		const bool failed = failRef == 0 && failFile == 0 && failSub == 0 && failNone == 0 && failNull == 0
			&& JsonEquals(before, model.ToJson());
		Report(results, name + " 失敗はモデルを変えない", failed, failed ? std::string() : "失敗するはずの挿入が通ったか、モデルが変わった");
	}


	/** T10 (b): DetachNode で外したサブツリーは子孫の editId を保ち、元の位置へ戻すと ToJson が元と一致する */
	void RunStructureDetachTest(TestResults& results)
	{
		const std::string name = "T10 (b) DetachNode";
		aq::level::LevelEditModel model;
		if (!LoadStructureTestModel(model)) { Report(results, name, false, "試験用 Level を読み込めない"); return; }

		const JsonValue original = model.ToJson();
		const uint32_t  idA   = IdOf(model, "A");
		const uint32_t  idA1  = IdOf(model, "A1");
		const uint32_t  idA1a = IdOf(model, "A1a");
		const uint32_t  idA2  = IdOf(model, "A2");

		aq::level::NodeParent parent{ 12345, 12345 };
		size_t                index = 12345;
		auto detached = model.DetachNode(idA, &parent, &index);
		if (!detached) { Report(results, name, false, "取り外せない"); return; }

		const bool idsKept = detached->editId == idA && detached->children.size() == 2
			&& detached->children[0]->editId == idA1 && detached->children[1]->editId == idA2
			&& detached->children[0]->children.size() == 1 && detached->children[0]->children[0]->editId == idA1a;
		Report(results, name + " editId を保つ", idsKept, idsKept ? std::string() : "取り外したサブツリーの editId が変わった");

		const std::string afterDetach = LevelOutline(model.ToJson());
		const bool        removed     = afterDetach == "B,Ref|[Inline:S1][file:Other.level.json]"
			&& model.FindNode(idA) == nullptr && model.FindNode(idA1a) == nullptr;
		Report(results, name + " 外した後", removed, "外した後の並び: " + afterDetach);

		const bool placeOk = parent.nodeId == 0 && parent.subLevelIndex == -1 && index == 0;
		Report(results, name + " 元の置き場所", placeOk,
			"nodeId " + std::to_string(parent.nodeId) + " / subLevelIndex " + std::to_string(parent.subLevelIndex) + " / index " + std::to_string(index));

		const uint32_t back     = model.InsertNode(parent, index, std::move(detached));
		const bool     restored = back == idA && JsonEquals(original, model.ToJson());
		Report(results, name + " 元の位置へ戻す", restored, restored ? std::string() : "戻した後の ToJson が元と違う");

		const bool found = model.FindNode(idA) != nullptr && model.FindNode(idA)->name == "A"
			&& model.FindNode(idA1a) != nullptr && model.FindNode(idA1a)->name == "A1a";
		Report(results, name + " FindNode", found, found ? std::string() : "戻した後に同じ ID で引けない");

		// const 版の FindNode と FindParent
		const aq::level::LevelEditModel& constModel = model;
		aq::level::NodeParent            childParent;
		size_t                           childIndex = 0;
		const bool hasParent = constModel.FindParent(idA1a, childParent, childIndex);
		Report(results, name + " FindParent", hasParent && childParent.nodeId == idA1 && childIndex == 0 && constModel.FindNode(idA1) != nullptr,
			"nodeId " + std::to_string(childParent.nodeId) + " / index " + std::to_string(childIndex));

		// 無い ID は nullptr でモデルを変えない
		const bool missing = model.DetachNode(99999) == nullptr && JsonEquals(original, model.ToJson());
		Report(results, name + " 無い ID", missing, "無い ID の取り外しがモデルを変えた");
	}


	/** T10 (c): MoveNode でトップ ↔ 子 ↔ subLevel のルートへ、子孫ごと移る */
	void RunStructureMoveTest(TestResults& results)
	{
		const std::string name = "T10 (c) MoveNode";
		aq::level::LevelEditModel model;
		if (!LoadStructureTestModel(model)) { Report(results, name, false, "試験用 Level を読み込めない"); return; }

		const uint32_t idA   = IdOf(model, "A");
		const uint32_t idA1  = IdOf(model, "A1");
		const uint32_t idA1a = IdOf(model, "A1a");
		const uint32_t idA2  = IdOf(model, "A2");
		const uint32_t idB   = IdOf(model, "B");

		// トップ → 子
		bool allMoved = model.MoveNode(idB, aq::level::NodeParent{ idA, -1 }, STRUCTURE_APPEND);
		ReportOutline(results, name + " トップ → 子", model, "A(A1(A1a),A2,B),Ref|[Inline:S1][file:Other.level.json]");

		// 子 → トップ(先頭)
		allMoved = model.MoveNode(idA2, aq::level::NodeParent{ 0, -1 }, 0) && allMoved;
		ReportOutline(results, name + " 子 → トップ", model, "A2,A(A1(A1a),B),Ref|[Inline:S1][file:Other.level.json]");

		// 子(孫つき)→ subLevel のルート。孫の ID が変わらず、親が A1 のまま
		allMoved = model.MoveNode(idA1, aq::level::NodeParent{ 0, STRUCTURE_INLINE_SUB }, STRUCTURE_APPEND) && allMoved;
		ReportOutline(results, name + " 子 → subLevel", model, "A2,A(B),Ref|[Inline:S1,A1(A1a)][file:Other.level.json]");
		aq::level::NodeParent parent;
		size_t                index = 0;
		const bool subOk = model.FindParent(idA1, parent, index) && parent.nodeId == 0 && parent.subLevelIndex == STRUCTURE_INLINE_SUB && index == 1
			&& model.FindParent(idA1a, parent, index) && parent.nodeId == idA1 && index == 0
			&& model.FindNode(idA1a) != nullptr && model.FindNode(idA1a)->name == "A1a";
		Report(results, name + " subLevel の中の親子", subOk, subOk ? std::string() : "subLevel へ移した後の親・ID が違う");

		// subLevel のルート → 別のノードの子(subLevel をまたぐ)
		allMoved = model.MoveNode(idA1, aq::level::NodeParent{ idA, -1 }, 0) && allMoved;
		ReportOutline(results, name + " subLevel → 子", model, "A2,A(A1(A1a),B),Ref|[Inline:S1][file:Other.level.json]");

		// 同じ列の中で後ろへ動かすときの index は、取り外した後の添字([A2, A, Ref] から A2 を index 1 へ → A, A2, Ref)
		allMoved = model.MoveNode(idA2, aq::level::NodeParent{ 0, -1 }, 1) && allMoved;
		ReportOutline(results, name + " 同じ列の中で後ろへ", model, "A(A1(A1a),B),A2,Ref|[Inline:S1][file:Other.level.json]");

		// 同じ親の子の中で、末尾へ
		allMoved = model.MoveNode(idA1, aq::level::NodeParent{ idA, -1 }, STRUCTURE_APPEND) && allMoved;
		ReportOutline(results, name + " 同じ親の末尾へ", model, "A(B,A1(A1a)),A2,Ref|[Inline:S1][file:Other.level.json]");

		// 参照ノード自身は、行き先が有効なら移せる
		allMoved = model.MoveNode(IdOf(model, "Ref"), aq::level::NodeParent{ idA2, -1 }, 0) && allMoved;
		ReportOutline(results, name + " 参照ノードを動かす", model, "A(B,A1(A1a)),A2(Ref)|[Inline:S1][file:Other.level.json]");

		Report(results, name + " 戻り値", allMoved, "有効な移動が false を返した");
	}


	/** T10 (d): 自分・子孫・参照ノード・ファイル参照の subLevel への移動と、参照ノードへの挿入は false でモデルが変わらない */
	void RunStructureRejectTest(TestResults& results)
	{
		const std::string name = "T10 (d) 受け付けない操作";
		aq::level::LevelEditModel model;
		if (!LoadStructureTestModel(model)) { Report(results, name, false, "試験用 Level を読み込めない"); return; }

		const JsonValue original = model.ToJson();
		const uint32_t  idA   = IdOf(model, "A");
		const uint32_t  idA1a = IdOf(model, "A1a");
		const uint32_t  idB   = IdOf(model, "B");
		const uint32_t  idRef = IdOf(model, "Ref");

		struct Case
		{
			const char* label;
			bool        result;
		};
		const Case cases[] = {
			{ "自分の下へ",                 model.MoveNode(idA, aq::level::NodeParent{ idA, -1 }, 0) },
			{ "子孫の下へ",                 model.MoveNode(idA, aq::level::NodeParent{ idA1a, -1 }, 0) },
			{ "参照ノードの下へ",           model.MoveNode(idB, aq::level::NodeParent{ idRef, -1 }, 0) },
			{ "ファイル参照の subLevel へ", model.MoveNode(idB, aq::level::NodeParent{ 0, STRUCTURE_FILE_SUB }, 0) },
			{ "範囲外の subLevel へ",       model.MoveNode(idB, aq::level::NodeParent{ 0, 9 }, 0) },
			{ "無いノードの下へ",           model.MoveNode(idB, aq::level::NodeParent{ 99999, -1 }, 0) },
			{ "無いノードを移す",           model.MoveNode(99999, aq::level::NodeParent{ 0, -1 }, 0) },
		};
		bool        allRejected = true;
		std::string accepted;
		for (const Case& c : cases) {
			if (c.result) {
				allRejected = false;
				accepted += std::string(accepted.empty() ? "" : ", ") + c.label;
			}
		}
		Report(results, name + " 移動は false", allRejected, allRejected ? std::string() : "通ってしまった: " + accepted);

		const uint32_t insertedUnderRef = model.InsertNode(aq::level::NodeParent{ idRef, -1 }, 0, MakeEntityNode("X"));
		Report(results, name + " 参照ノードへの挿入は 0", insertedUnderRef == 0, "ID " + std::to_string(insertedUnderRef) + " を返した");

		const bool unchanged = JsonEquals(original, model.ToJson());
		Report(results, name + " モデルは変わらない", unchanged, unchanged ? std::string() : "ToJson が元と違う: " + LevelOutline(model.ToJson()));
	}


	/** T10 (e): SetNodeName */
	void RunStructureRenameTest(TestResults& results)
	{
		const std::string name = "T10 (e) SetNodeName";
		aq::level::LevelEditModel model;
		if (!LoadStructureTestModel(model)) { Report(results, name, false, "試験用 Level を読み込めない"); return; }

		const bool renamed = model.SetNodeName(IdOf(model, "A1a"), "Renamed")
			&& model.SetNodeName(IdOf(model, "Ref"), "RefRenamed")
			&& model.SetNodeName(IdOf(model, "S1"), "S1Renamed");
		Report(results, name + " 戻り値", renamed, "false を返した");
		ReportOutline(results, name, model, "A(A1(Renamed),A2),B,RefRenamed|[Inline:S1Renamed][file:Other.level.json]");

		const JsonValue before  = model.ToJson();
		const bool      missing = model.SetNodeName(99999, "X");
		Report(results, name + " 無い ID", !missing && JsonEquals(before, model.ToJson()), "無い ID の変更が通った / モデルが変わった");
	}


	/** T10 (e'): HasRelativeReferences は、相対の prefab / subLevel の path があるときだけ true */
	void RunStructureRelativeReferenceTest(TestResults& results)
	{
		struct Case
		{
			const char* label;
			const char* json;
			bool        expected;
		};
		const Case cases[] = {
			{ "参照なし", R"({"entities":[{"name":"A","components":{"Transform":{}}}],"subLevels":[{"level":{"name":"I","entities":[{"name":"S"}]}}]})", false },
			{ "絶対パスだけ", R"({"entities":[{"name":"R","prefab":"/abs/x.prefab.json"},{"name":"D","prefab":"C:/abs/y.prefab.json"},{"name":"W","prefab":"C:\\abs\\z.prefab.json"}],"subLevels":[{"level":"D:/abs/a.level.json"},{"level":"/abs/b.level.json"}]})", false },
			{ "トップの相対 prefab", R"({"entities":[{"name":"R","prefab":"Assets/x.prefab.json"}]})", true },
			{ "子孫の相対 prefab", R"({"entities":[{"name":"A","children":[{"name":"B","children":[{"name":"R","prefab":"x.prefab.json"}]}]}]})", true },
			{ "インライン subLevel の相対 prefab", R"({"entities":[],"subLevels":[{"level":{"name":"I","entities":[{"name":"R","prefab":"x.prefab.json"}]}}]})", true },
			{ "入れ子のインライン subLevel の相対 prefab", R"({"entities":[],"subLevels":[{"level":{"name":"I","entities":[],"subLevels":[{"level":{"name":"J","entities":[{"name":"R","prefab":"x.prefab.json"}]}}]}}]})", true },
			{ "ファイル参照の相対 path", R"({"entities":[],"subLevels":[{"level":"Other.level.json"}]})", true },
			{ "入れ子のファイル参照の相対 path", R"({"entities":[],"subLevels":[{"level":{"name":"I","entities":[],"subLevels":[{"level":"Other.level.json"}]}}]})", true },
			{ "絶対と相対が混ざる", R"({"entities":[{"name":"R","prefab":"/abs/x.prefab.json"}],"subLevels":[{"level":"Other.level.json"}]})", true },
		};
		for (const Case& c : cases) {
			const std::string         name = std::string("T10 (e') HasRelativeReferences ") + c.label;
			aq::level::LevelEditModel model;
			if (!model.FromJson(aq::util::JsonParser::ParseString(c.json))) {
				Report(results, name, false, "試験用 Level を読み込めない");
				continue;
			}
			const bool actual = model.HasRelativeReferences();
			Report(results, name, actual == c.expected, std::string("期待 ") + (c.expected ? "true" : "false"));
		}

		// 操作の結果が反映される(相対の参照ノードを外すと false、戻すと true)
		aq::level::LevelEditModel model;
		const std::string         name = "T10 (e') 操作の結果が反映される";
		if (!model.FromJson(aq::util::JsonParser::ParseString(R"({"entities":[{"name":"R","prefab":"x.prefab.json"}]})"))) {
			Report(results, name, false, "試験用 Level を読み込めない");
			return;
		}
		auto       detached    = model.DetachNode(IdOf(model, "R"));
		const bool afterDetach = model.HasRelativeReferences();
		model.InsertNode(aq::level::NodeParent{ 0, -1 }, 0, std::move(detached));
		const bool afterInsert = model.HasRelativeReferences();
		Report(results, name, !afterDetach && afterInsert,
			std::string("外した後 ") + (afterDetach ? "true" : "false") + " / 戻した後 " + (afterInsert ? "true" : "false"));
	}


	/** T10 (f): 操作の後に ToJson → FromJson → ToJson が一致する(保存できる形のまま) */
	void RunStructureRoundTripTest(TestResults& results)
	{
		const std::string name = "T10 (f) 操作後の往復";
		aq::level::LevelEditModel model;
		if (!LoadStructureTestModel(model)) { Report(results, name, false, "試験用 Level を読み込めない"); return; }

		model.InsertNode(aq::level::NodeParent{ IdOf(model, "A1"), -1 }, STRUCTURE_APPEND, MakeEntityNode("N"));
		model.MoveNode(IdOf(model, "A1"), aq::level::NodeParent{ 0, STRUCTURE_INLINE_SUB }, 0);
		model.MoveNode(IdOf(model, "B"), aq::level::NodeParent{ IdOf(model, "A"), -1 }, 0);
		model.SetNodeName(IdOf(model, "A2"), "A2Renamed");
		auto detached = model.DetachNode(IdOf(model, "S1"));
		model.InsertNode(aq::level::NodeParent{ 0, -1 }, STRUCTURE_APPEND, std::move(detached));

		const JsonValue           first = model.ToJson();
		aq::level::LevelEditModel model2;
		if (!model2.FromJson(first)) { Report(results, name, false, "読み直せない"); return; }
		const JsonValue second = model2.ToJson();
		const bool      stable = JsonEquals(first, second);
		Report(results, name, stable, stable ? std::string() : "1 回目と 2 回目で違う: " + DiffKeys(first, second));
	}


	/**
	 * T10 構造操作
	 *
	 * LevelEditModel のノードの挿入・取り外し・移動・名前の変更と、相対参照の判定を、画面も Engine も作らずに試す
	 * (モデルの JSON と ComponentRegistry だけを使う)。
	 */
	void RunStructureEditTests(TestResults& results)
	{
		RunStructureInsertTest(results);
		RunStructureDetachTest(results);
		RunStructureMoveTest(results);
		RunStructureRejectTest(results);
		RunStructureRenameTest(results);
		RunStructureRelativeReferenceTest(results);
		RunStructureRoundTripTest(results);
	}


	/** T11 で参照ノードを持つ試験用 Level(T6_Overrides.level.json)の、参照ノードの名前(RefFull は overrides.name が実行時の名前) */
	static constexpr const char* VALUE_REF_FULL_NAME  = "RefFullRenamed";
	static constexpr const char* VALUE_REF_PLAIN_NAME = "RefPlain";


	/** T11 の試験用 Level(TestData の T6_Overrides.level.json)を読み込む。基準ディレクトリは TestData になる */
	bool LoadValueTestModel(aq::level::LevelEditModel& model)
	{
		const std::string path = std::string(TEST_DATA_DIR) + "/" + OVERRIDE_TEST_LEVEL;
		return model.LoadFile(path);
	}


	/** name のノードを書き出した JSON(無ければ Null) */
	JsonValue NodeJsonOf(const aq::level::LevelEditModel& model, const std::string& name)
	{
		const PrefabEditNode* node = model.FindNode(IdOf(model, name));
		return node ? aq::ecs::PrefabNodeToJson(*node) : JsonValue();
	}


	/** 参照ノード 1 つだけの Level の JSON(参照ノードの名前は R) */
	JsonValue MakeReferenceLevelJson(const std::string& prefabPath)
	{
		JsonValue entity = JsonValue::MakeObject();
		entity.Set("name", JsonValue(std::string("R")));
		entity.Set("prefab", JsonValue(prefabPath));
		JsonValue entities = JsonValue::MakeArray();
		entities.PushBack(std::move(entity));
		JsonValue level = JsonValue::MakeObject();
		level.Set("name", JsonValue(std::string("T11_Ref")));
		level.Set("entities", std::move(entities));
		return level;
	}


	/** 2 つのノードの JSON が一字一句同じかを 1 件として出す */
	void ReportNodeEquals(TestResults& results, const std::string& name, const JsonValue& actual, const JsonValue& expected)
	{
		const bool passed = JsonEqualsExact(actual, expected);
		Report(results, name, passed, passed ? std::string() : "期待 " + StringifyOneLine(expected) + " / 実際 " + StringifyOneLine(actual));
	}


	/** T11 (a): 実体ノードの SetComponentField(float・Vector3・パス)で、そのキーだけが変わる */
	void RunValueEntityTest(TestResults& results)
	{
		const std::string name = "T11 (a) 実体ノードの SetComponentField";
		aq::level::LevelEditModel model;
		if (!LoadValueTestModel(model)) { Report(results, name, false, "試験用 Level を読み込めない"); return; }

		const uint32_t  idChild      = IdOf(model, "InlineChild");
		const uint32_t  idInline     = IdOf(model, "Inline");
		const JsonValue beforeChild  = NodeJsonOf(model, "InlineChild");

		// float(Spawner.interval)
		const bool floatSet = model.SetComponentField(idChild, "Spawner", "interval", JsonValue(3.0));
		JsonValue  spawner  = beforeChild["components"]["Spawner"];
		spawner.Set("interval", JsonValue(3.0));
		JsonValue expectedChild = WithMember(beforeChild, "components", "Spawner", spawner);
		ReportNodeEquals(results, name + " float", NodeJsonOf(model, "InlineChild"), expectedChild);

		// パス(Spawner.prefab。文字列を入れるだけでリソースは読まない)
		const bool pathSet = model.SetComponentField(idChild, "Spawner", "prefab", JsonValue(std::string("x.prefab.json")));
		spawner.Set("prefab", JsonValue(std::string("x.prefab.json")));
		expectedChild = WithMember(beforeChild, "components", "Spawner", spawner);
		ReportNodeEquals(results, name + " パス", NodeJsonOf(model, "InlineChild"), expectedChild);

		// Vector3(Transform.position)
		// Inline は子(InlineChild)を含むので、期待値は子を変えた後の JSON から作る
		const JsonValue inlineBeforeVector = NodeJsonOf(model, "Inline");
		const JsonValue position     = aq::util::JsonParser::ParseString("[4,5,6]");
		const bool      vectorSet    = model.SetComponentField(idInline, "Transform", "position", position);
		JsonValue       transform    = inlineBeforeVector["components"]["Transform"];
		transform.Set("position", position);
		ReportNodeEquals(results, name + " Vector3", NodeJsonOf(model, "Inline"), WithMember(inlineBeforeVector, "components", "Transform", transform));

		// ほかのノードは変わらない
		ReportNodeEquals(results, name + " ほかのノードは変わらない", NodeJsonOf(model, "InlineChild"), expectedChild);
		Report(results, name + " 戻り値", floatSet && pathSet && vectorSet, "false を返した");
	}


	/** T11 (b): 参照ノードで override の無いコンポーネントのキーを変えると、そのキーだけが overrides.components.<型> に増える */
	void RunValueReferenceNewOverrideTest(TestResults& results)
	{
		const std::string name = "T11 (b) override の無いコンポーネント";
		aq::level::LevelEditModel model;
		if (!LoadValueTestModel(model)) { Report(results, name, false, "試験用 Level を読み込めない"); return; }

		const uint32_t id = IdOf(model, VALUE_REF_PLAIN_NAME);
		const bool     ok = model.SetComponentField(id, "Spawner", "interval", JsonValue(2.0));
		const JsonValue expected = aq::util::JsonParser::ParseString(R"({"components":{"Spawner":{"interval":2}}})");
		ReportOverrides(results, name, NodeJsonOf(model, VALUE_REF_PLAIN_NAME), expected);
		Report(results, name + " 戻り値", ok, "false を返した");

		// 参照先の名前とパスは変わらない
		const JsonValue written = NodeJsonOf(model, VALUE_REF_PLAIN_NAME);
		Report(results, name + " prefab", written["prefab"].IsString() && written["prefab"].AsString() == "T6_Base.prefab.json",
			"prefab: " + written["prefab"].AsString());
	}


	/** T11 (c): override のある(出どころ components)コンポーネントでは、そのキーだけが足される */
	void RunValueReferenceExistingOverrideTest(TestResults& results)
	{
		const std::string name = "T11 (c) override のあるコンポーネント";
		aq::level::LevelEditModel model;
		if (!LoadValueTestModel(model)) { Report(results, name, false, "試験用 Level を読み込めない"); return; }

		const std::string levelPath = std::string(TEST_DATA_DIR) + "/" + OVERRIDE_TEST_LEVEL;
		const JsonValue   level     = aq::util::JsonParser::ParseFile(levelPath.c_str());
		const JsonValue*  source    = FindEntity(level, OVERRIDE_TEST_NODE);
		if (!source) { Report(results, name, false, "試験用のノードが無い"); return; }
		const JsonValue& raw = OverridesOf(*source);

		// Transform.scale(override に無いキー)を変える。position の override は一字一句変わらない
		const uint32_t id = IdOf(model, VALUE_REF_FULL_NAME);
		const bool     ok = model.SetComponentField(id, "Transform", "scale", aq::util::JsonParser::ParseString("[2,2,2]"));
		JsonValue      transformWant = raw["components"]["Transform"];
		transformWant.Set("scale", aq::util::JsonParser::ParseString("[2,2,2]"));
		ReportOverrides(results, name, NodeJsonOf(model, VALUE_REF_FULL_NAME), WithMember(raw, "components", "Transform", transformWant));
		Report(results, name + " 戻り値", ok, "false を返した");
	}


	/** T11 (c'): 付け替え。Prefab の値(3)を既定値(-1)に変えるとそのキーが書かれ、3 → 5 → 3 は何も増えない */
	void RunValueRebaseTest(TestResults& results)
	{
		const std::string name = "T11 (c') 基準の付け替え";

		// Prefab で 3(既定値は -1)・override 無し。-1 にすると、-1 で書かれる
		{
			aq::level::LevelEditModel model;
			if (!LoadValueTestModel(model)) { Report(results, name, false, "試験用 Level を読み込めない"); return; }
			const uint32_t id = IdOf(model, VALUE_REF_PLAIN_NAME);
			const bool     ok = model.SetComponentField(id, "Spawner", "maxCount", JsonValue(-1.0));
			ReportOverrides(results, name + " 3 → -1", NodeJsonOf(model, VALUE_REF_PLAIN_NAME),
				aq::util::JsonParser::ParseString(R"({"components":{"Spawner":{"maxCount":-1}}})"));
			Report(results, name + " 3 → -1 の戻り値", ok, "false を返した");
		}

		// 3 → 5 → 3 は、overrides を作らない(空の components も作らない)
		{
			aq::level::LevelEditModel model;
			if (!LoadValueTestModel(model)) { Report(results, name, false, "試験用 Level を読み込めない"); return; }
			const uint32_t id = IdOf(model, VALUE_REF_PLAIN_NAME);
			const bool     ok = model.SetComponentField(id, "Spawner", "maxCount", JsonValue(5.0))
				&& model.SetComponentField(id, "Spawner", "maxCount", JsonValue(3.0));
			ReportOverrides(results, name + " 3 → 5 → 3", NodeJsonOf(model, VALUE_REF_PLAIN_NAME), JsonValue::MakeObject());
			Report(results, name + " 3 → 5 → 3 の戻り値", ok, "false を返した");
		}

		// override の疎な JSON にあるキー(interval)を持つコンポーネントで、別のキー(maxCount)を変えても、既存のキーは一字一句変わらない
		{
			aq::level::LevelEditModel model;
			if (!LoadValueTestModel(model)) { Report(results, name, false, "試験用 Level を読み込めない"); return; }

			const std::string levelPath = std::string(TEST_DATA_DIR) + "/" + OVERRIDE_TEST_LEVEL;
			const JsonValue   level     = aq::util::JsonParser::ParseFile(levelPath.c_str());
			const JsonValue*  source    = FindEntity(level, OVERRIDE_TEST_NODE);
			if (!source) { Report(results, name, false, "試験用のノードが無い"); return; }
			const JsonValue& raw = OverridesOf(*source);

			const uint32_t id = IdOf(model, VALUE_REF_FULL_NAME);
			const bool     ok = model.SetComponentField(id, "Spawner", "maxCount", JsonValue(7.0));
			JsonValue      spawnerWant = raw["components"]["Spawner"];
			spawnerWant.Set("maxCount", JsonValue(7.0));
			ReportOverrides(results, name + " 既存のキーを保つ", NodeJsonOf(model, VALUE_REF_FULL_NAME),
				WithMember(raw, "components", "Spawner", spawnerWant));
			Report(results, name + " 既存のキーを保つ 戻り値", ok, "false を返した");
		}
	}


	/** T11 (d): 変えて元に戻すと元と一致する(実体ノードも参照ノードも) */
	void RunValueRevertTest(TestResults& results)
	{
		const std::string name = "T11 (d) 変えて元に戻す";
		aq::level::LevelEditModel model;
		if (!LoadValueTestModel(model)) { Report(results, name, false, "試験用 Level を読み込めない"); return; }

		// 参照ノードの元の値は、Prefab 側の値を含む解決済みの値から取る
		const uint32_t idFull  = IdOf(model, VALUE_REF_FULL_NAME);
		const uint32_t idPlain = IdOf(model, VALUE_REF_PLAIN_NAME);
		JsonValue      resolvedFull;
		JsonValue      resolvedPlain;
		if (!model.ResolveReferenceComponents(idFull, resolvedFull) || !model.ResolveReferenceComponents(idPlain, resolvedPlain)) {
			Report(results, name, false, "参照ノードを解決できない");
			return;
		}
		// 解決済みの値は Prefab ファイルに書かれた疎な JSON なので、キーが無ければ既定値が元の値になる
		const JsonValue originalScale    = resolvedFull["Transform"].Contains("scale")
			? resolvedFull["Transform"]["scale"] : aq::util::JsonParser::ParseString("[1,1,1]");
		const JsonValue originalPosition = resolvedPlain["Transform"].Contains("position")
			? resolvedPlain["Transform"]["position"] : aq::util::JsonParser::ParseString("[0,0,0]");

		const JsonValue original = model.ToJson();
		const bool ok =
			model.SetComponentField(IdOf(model, "InlineChild"), "Spawner", "interval", JsonValue(9.0))
			&& model.SetComponentField(IdOf(model, "InlineChild"), "Spawner", "interval", JsonValue(0.5))
			&& model.SetComponentField(idFull, "Transform", "scale", aq::util::JsonParser::ParseString("[2,2,2]"))
			&& model.SetComponentField(idFull, "Transform", "scale", originalScale)
			&& model.SetComponentField(idPlain, "Transform", "position", aq::util::JsonParser::ParseString("[9,9,9]"))
			&& model.SetComponentField(idPlain, "Transform", "position", originalPosition);
		Report(results, name + " 戻り値", ok, "false を返した");

		const JsonValue after = model.ToJson();
		const bool      same  = JsonEqualsExact(original, after);
		Report(results, name, same, same ? std::string() : "元と違う: " + StringifyOneLine(after));
	}


	/** T11 (e): ResolveReferenceComponents が Prefab 側の値と override を反映する。直接の参照先が無いと false */
	void RunValueResolveTest(TestResults& results)
	{
		namespace fs = std::filesystem;

		const std::string name = "T11 (e) ResolveReferenceComponents";
		{
			aq::level::LevelEditModel model;
			if (!LoadValueTestModel(model)) { Report(results, name, false, "試験用 Level を読み込めない"); return; }

			// RefFull: override の値(Spawner.interval 2.5、Transform.position)と Prefab 側の値(Spawner.maxCount 3)、追加(AudioSource)、除去(AudioListener)
			JsonValue   full;
			std::string error;
			const bool  fullOk = model.ResolveReferenceComponents(IdOf(model, VALUE_REF_FULL_NAME), full, &error);
			const bool  fullValues = fullOk && full.IsObject()
				&& full["Spawner"]["interval"].AsDouble() == 2.5 && full["Spawner"]["maxCount"].AsDouble() == 3.0
				&& full["Transform"]["position"][0].AsDouble() == 1.5
				&& full.Contains("AudioSource") && !full.Contains("AudioListener");
			Report(results, name + " override あり", fullValues, fullOk ? "解決した値: " + StringifyOneLine(full) : error);

			// RefPlain: Prefab 側の値そのまま
			JsonValue  plain;
			const bool plainOk = model.ResolveReferenceComponents(IdOf(model, VALUE_REF_PLAIN_NAME), plain, &error);
			const bool plainValues = plainOk && plain.IsObject()
				&& plain["Spawner"]["interval"].AsDouble() == 1.0 && plain["Spawner"]["maxCount"].AsDouble() == 3.0
				&& plain.Contains("AudioListener");
			Report(results, name + " override なし", plainValues, plainOk ? "解決した値: " + StringifyOneLine(plain) : error);

			// 編集した値も反映される(まだ保存していない編集内容で解決する)
			model.SetComponentField(IdOf(model, VALUE_REF_PLAIN_NAME), "Spawner", "maxCount", JsonValue(8.0));
			JsonValue  edited;
			const bool editedOk = model.ResolveReferenceComponents(IdOf(model, VALUE_REF_PLAIN_NAME), edited, &error);
			Report(results, name + " 編集内容", editedOk && edited["Spawner"]["maxCount"].AsDouble() == 8.0,
				editedOk ? "解決した値: " + StringifyOneLine(edited) : error);

			// 参照ノードでない・無い ID は false
			JsonValue  none;
			const bool notRef = !model.ResolveReferenceComponents(IdOf(model, "Inline"), none, &error)
				&& !model.ResolveReferenceComponents(99999, none, nullptr);
			Report(results, name + " 参照ノードでない", notRef, "true を返した");
		}

		// 直接の参照先が無い → false で、error に理由が入る。
		// 読めないファイルの ParseFile はアセットパスの候補を探すときに Engine を引くので、その間だけ Engine を作る(T8 と同じ)
		{
			const bool engineCreatedHere = !aq::Engine::IsCreated();
			if (engineCreatedHere) aq::Engine::Create();
			aq::level::LevelEditModel model;
			model.FromJson(MakeReferenceLevelJson("NoSuchDirectory/NoSuch.prefab.json"));
			JsonValue   out;
			std::string error;
			const bool  failed = !model.ResolveReferenceComponents(IdOf(model, "R"), out, &error);
			Report(results, name + " 参照先が無い", failed && !error.empty(), failed ? "error が空" : "true を返した");
			if (engineCreatedHere) aq::Engine::Release();
		}

		// コンポーネントを持たない Prefab → true で空のオブジェクト
		{
			std::error_code ec;
			const fs::path  emptyPath = fs::temp_directory_path(ec) / "ReflectionTest_T11_Empty.prefab.json";
			JsonValue       emptyPrefab = JsonValue::MakeObject();
			emptyPrefab.Set("name", JsonValue(std::string("Empty")));
			if (!aq::util::JsonSerializer::WriteFile(emptyPath.string().c_str(), emptyPrefab)) {
				Report(results, name + " コンポーネントを持たない Prefab", false, "一時ファイルを書けない: " + emptyPath.string());
			} else {
				aq::level::LevelEditModel model;
				model.FromJson(MakeReferenceLevelJson(emptyPath.generic_string()));
				JsonValue   out;
				std::string error;
				const bool  ok = model.ResolveReferenceComponents(IdOf(model, "R"), out, &error);
				Report(results, name + " コンポーネントを持たない Prefab", ok && out.IsObject() && out.GetObject().empty(),
					ok ? "解決した値: " + StringifyOneLine(out) : error);
			}
			fs::remove(emptyPath, ec);
		}
	}


	/** T11 (f): AddComponent / RemoveComponent(Transform は消せない、参照ノードは false) */
	void RunValueAddRemoveTest(TestResults& results)
	{
		const std::string name = "T11 (f) AddComponent / RemoveComponent";
		aq::level::LevelEditModel model;
		if (!LoadValueTestModel(model)) { Report(results, name, false, "試験用 Level を読み込めない"); return; }

		const uint32_t  idChild      = IdOf(model, "InlineChild");   // Transform と Spawner を持つ実体ノード
		const JsonValue beforeChild  = NodeJsonOf(model, "InlineChild");

		// 足す: 既定値 / JSON 付き。既に持つ・未登録は false
		const bool addedDefault  = model.AddComponent(idChild, "AudioListener");
		const bool addedAgain    = model.AddComponent(idChild, "AudioListener");
		const JsonValue audioJson = aq::util::JsonParser::ParseString(R"({"volume":0.25})");
		const bool addedWithJson = model.AddComponent(idChild, "AudioSource", &audioJson);
		const bool addedUnknown  = model.AddComponent(idChild, "NoSuchType");
		const JsonValue added    = NodeJsonOf(model, "InlineChild");
		const bool addedOk = addedDefault && !addedAgain && addedWithJson && !addedUnknown
			&& added["components"].Contains("AudioListener")
			&& added["components"]["AudioSource"]["volume"].AsDouble() == 0.25
			&& added["components"].GetObject().size() == beforeChild["components"].GetObject().size() + 2;
		Report(results, name + " 足す", addedOk, "足した後: " + StringifyOneLine(added));

		// 消す: Transform(addable == false)は消せない。持っていない型・未登録も false
		const JsonValue beforeRemove = NodeJsonOf(model, "InlineChild");
		const bool removedTransform = model.RemoveComponent(idChild, "Transform");
		const bool removedUnknown   = model.RemoveComponent(idChild, "NoSuchType");
		const bool removedMissing   = model.RemoveComponent(IdOf(model, "Inline"), "Spawner");
		Report(results, name + " 消せない型", !removedTransform && !removedUnknown && !removedMissing
			&& JsonEqualsExact(beforeRemove, NodeJsonOf(model, "InlineChild")), "消せるはずのない型が消えた / モデルが変わった");

		// 消す: 外す前の値が outJson に入る。2 回目は false
		JsonValue  removedJson;
		const bool removed      = model.RemoveComponent(idChild, "Spawner", &removedJson);
		const bool removedAgain = model.RemoveComponent(idChild, "Spawner");
		const JsonValue afterRemove = NodeJsonOf(model, "InlineChild");
		const bool removeOk = removed && !removedAgain && JsonEqualsExact(removedJson, beforeRemove["components"]["Spawner"])
			&& !afterRemove["components"].Contains("Spawner") && afterRemove["components"].Contains("Transform");
		Report(results, name + " 消す", removeOk, "外した値: " + StringifyOneLine(removedJson));

		// 参照ノードは足せも消せもない(モデルは変わらない)
		const JsonValue beforeRef  = model.ToJson();
		const bool refAdd    = model.AddComponent(IdOf(model, VALUE_REF_PLAIN_NAME), "AudioSource");
		const bool refRemove = model.RemoveComponent(IdOf(model, VALUE_REF_FULL_NAME), "Spawner");
		Report(results, name + " 参照ノード", !refAdd && !refRemove && JsonEqualsExact(beforeRef, model.ToJson()),
			"参照ノードの追加・削除が通った / モデルが変わった");
	}


	/** T11 (h): 失敗する SetComponentField は false を返し、モデルを変えない */
	void RunValueRejectTest(TestResults& results)
	{
		const std::string name = "T11 (h) 失敗はモデルを変えない";
		aq::level::LevelEditModel model;
		if (!LoadValueTestModel(model)) { Report(results, name, false, "試験用 Level を読み込めない"); return; }

		const JsonValue original = model.ToJson();
		struct Case
		{
			const char* label;
			bool        result;
		};
		const Case cases[] = {
			{ "未登録の型",                     model.SetComponentField(IdOf(model, "Inline"), "NoSuchType", "x", JsonValue(1.0)) },
			{ "実体ノードが持たない型",         model.SetComponentField(IdOf(model, "Inline"), "Spawner", "interval", JsonValue(1.0)) },
			{ "参照先から除かれた型",           model.SetComponentField(IdOf(model, VALUE_REF_FULL_NAME), "AudioListener", "x", JsonValue(1.0)) },
			{ "参照先に無い型",                 model.SetComponentField(IdOf(model, VALUE_REF_PLAIN_NAME), "AudioSource", "volume", JsonValue(0.1)) },
			{ "無い ID",                        model.SetComponentField(99999, "Transform", "position", JsonValue(1.0)) },
		};
		std::string accepted;
		for (const Case& c : cases) {
			if (c.result) accepted += std::string(accepted.empty() ? "" : ", ") + c.label;
		}
		Report(results, name + " false", accepted.empty(), "通ってしまった: " + accepted);
		Report(results, name + " 変わらない", JsonEqualsExact(original, model.ToJson()), "ToJson が元と違う");

		// 直接の参照先が無い参照ノード
		aq::level::LevelEditModel missing;
		missing.FromJson(MakeReferenceLevelJson("NoSuchDirectory/NoSuch.prefab.json"));
		const JsonValue missingOriginal = missing.ToJson();
		const bool      missingSet      = missing.SetComponentField(IdOf(missing, "R"), "Spawner", "maxCount", JsonValue(1.0));
		Report(results, name + " 参照先が無い", !missingSet && JsonEqualsExact(missingOriginal, missing.ToJson()),
			"参照先が無いのに通った / モデルが変わった");
	}


	/**
	 * T11 値の操作((a)〜(f)と(h)。(g)は Engine を作る T8 の環境の中で行う)
	 *
	 * LevelEditModel のコンポーネントの値・追加・削除と参照ノードの解決を、画面も Engine も作らずに試す
	 * (TestData の Level と Prefab を読む)。
	 */
	void RunValueEditTests(TestResults& results)
	{
		RunValueEntityTest(results);
		RunValueReferenceNewOverrideTest(results);
		RunValueReferenceExistingOverrideTest(results);
		RunValueRebaseTest(results);
		RunValueRevertTest(results);
		RunValueResolveTest(results);
		RunValueAddRemoveTest(results);
		RunValueRejectTest(results);
	}


	/** 1 試験の結果を出す。説明は失敗したときだけ出す */
	void ReportOnFailure(TestResults& results, const std::string& testName, const bool passed, const std::string& detail)
	{
		Report(results, testName, passed, passed ? std::string() : detail);
	}


	/**
	 * T13 (e) で使うテスト用のコマンド
	 *
	 * モデルに触れずに、Execute / Undo の成否だけを決められる。
	 * failUndo なら Undo が false、failRedo なら 2 回目以降の Execute(やり直し)が false を返す。
	 */
	class HistoryTestCommand final : public editor::IEditorCommand
	{
	private:
		/** 失敗させる操作 */
		bool failUndo_;
		bool failRedo_;

		/** Execute を呼ばれた回数 */
		uint32_t executeCount_ = 0;


	public:
		HistoryTestCommand(const bool failUndo, const bool failRedo)
			: failUndo_(failUndo)
			, failRedo_(failRedo)
		{
		}

		bool Execute(aq::level::LevelEditModel& model) override
		{
			(void)model;
			++executeCount_;
			return !(failRedo_ && executeCount_ > 1);
		}

		bool Undo(aq::level::LevelEditModel& model) override
		{
			(void)model;
			return !failUndo_;
		}

		const char* GetName() const override { return "History Test"; }
	};


	/** 成功するテスト用のコマンドを 1 つ実行して履歴に積む */
	void PushHistoryTestCommand(editor::CommandHistory& history, aq::level::LevelEditModel& model,
	                            const bool failUndo = false, const bool failRedo = false)
	{
		auto command = std::make_unique<HistoryTestCommand>(failUndo, failRedo);
		command->Execute(model);
		history.Push(std::move(command));
	}


	/** ノードのコンポーネントの型の並び(ToJson の components はオブジェクトで並びが出ないので、編集ノードから直接取る) */
	std::vector<size_t> ComponentTypeOrder(const aq::level::LevelEditModel& model, const uint32_t id)
	{
		std::vector<size_t> order;
		if (const PrefabEditNode* node = model.FindNode(id)) {
			for (const PrefabEditComponent& component : node->components) {
				order.push_back(component.data.Type().GetHash());
			}
		}
		return order;
	}


	/** フィールドの今の値を double で返す(取れなければ NaN) */
	double FieldAsDouble(const aq::level::LevelEditModel& model, const uint32_t id, const char* typeName, const char* key)
	{
		JsonValue value;
		if (!model.GetComponentField(id, typeName, key, value) || !value.IsNumber()) return std::nan("");
		return value.AsDouble();
	}


	/**
	 * コマンド 1 つで Execute → Undo → Redo(Execute をもう一度)を行い、ToJson が
	 * 「変わる → 元と一致 → 実行後と一致」になるかを確かめる(比較は JsonEqualsExact)。
	 * expectedOutline が空でなければ、実行後のノードの並びも確かめる
	 */
	void CheckUndoRedo(TestResults& results, const std::string& name, aq::level::LevelEditModel& model,
	                   editor::IEditorCommand& command, const std::string& expectedOutline = std::string())
	{
		const JsonValue original = model.ToJson();

		const bool      executed     = command.Execute(model);
		const JsonValue executedJson = model.ToJson();
		const bool      changed      = !JsonEqualsExact(original, executedJson);
		Report(results, name + " Execute", executed && changed,
			executed && changed ? std::string() : (executed ? "ToJson が変わらない" : "false を返した"));
		if (!expectedOutline.empty()) {
			ReportOutline(results, name + " 実行後の並び", model, expectedOutline);
		}

		const bool      undone     = command.Undo(model);
		const JsonValue undoneJson = model.ToJson();
		const bool      restored   = JsonEqualsExact(original, undoneJson);
		Report(results, name + " Undo", undone && restored,
			undone && restored ? std::string() : (undone ? "元と違う: " + StringifyOneLine(undoneJson) : std::string("false を返した")));

		const bool      redone     = command.Execute(model);
		const JsonValue redoneJson = model.ToJson();
		const bool      again      = JsonEqualsExact(executedJson, redoneJson);
		Report(results, name + " Redo", redone && again,
			redone && again ? std::string() : (redone ? "実行後と違う: " + StringifyOneLine(redoneJson) : std::string("false を返した")));
	}


	/** T13 (a): ノードの作成・削除・移動(別の親・同じ列の前へ・同じ列の後ろへ)・名前の変更 */
	void RunUndoRedoNodeTest(TestResults& results)
	{
		const std::string name = "T13 (a)";

		// 作成: やり直しで同じノード(同じ ID)が同じ位置に戻る
		{
			aq::level::LevelEditModel model;
			if (!LoadStructureTestModel(model)) { Report(results, name, false, "試験用 Level を読み込めない"); return; }
			editor::CreateNodeCommand command(aq::level::NodeParent{ IdOf(model, "A"), -1 }, 1, "New");
			CheckUndoRedo(results, name + " CreateNode", model, command, "A(A1(A1a),New,A2),B,Ref|[Inline:S1][file:Other.level.json]");

			// CheckUndoRedo の後はやり直した状態。もう一度取り消してやり直し、ID が変わらないかを見る
			const uint32_t createdId = command.GetCreatedId();
			const bool     undone    = command.Undo(model) && model.FindNode(createdId) == nullptr;
			const bool     redone    = command.Execute(model);
			const bool     sameId    = undone && redone && createdId != 0 && command.GetCreatedId() == createdId
				&& model.FindNode(createdId) != nullptr && model.FindNode(createdId)->name == "New";
			Report(results, name + " CreateNode やり直しで ID を保つ", sameId, sameId ? std::string() : "やり直した後に同じ ID で引けない");
		}

		// 削除: 取り消しで子孫の ID も元に戻る
		{
			aq::level::LevelEditModel model;
			if (!LoadStructureTestModel(model)) { Report(results, name, false, "試験用 Level を読み込めない"); return; }
			const uint32_t idA   = IdOf(model, "A");
			const uint32_t idA1a = IdOf(model, "A1a");
			editor::DeleteNodeCommand command(idA);
			const JsonValue original = model.ToJson();
			command.Execute(model);
			const bool undone  = command.Undo(model);
			const bool idsKept = undone && JsonEqualsExact(original, model.ToJson())
				&& model.FindNode(idA1a) != nullptr && model.FindNode(idA1a)->name == "A1a";
			Report(results, name + " DeleteNode 取り消しで子孫の ID を保つ", idsKept, idsKept ? std::string() : "取り消した後に子孫が同じ ID で引けない");
		}
		{
			aq::level::LevelEditModel model;
			if (!LoadStructureTestModel(model)) { Report(results, name, false, "試験用 Level を読み込めない"); return; }
			editor::DeleteNodeCommand command(IdOf(model, "A"));
			CheckUndoRedo(results, name + " DeleteNode", model, command, "B,Ref|[Inline:S1][file:Other.level.json]");
		}

		// 移動: 別の親へ
		{
			aq::level::LevelEditModel model;
			if (!LoadStructureTestModel(model)) { Report(results, name, false, "試験用 Level を読み込めない"); return; }
			editor::MoveNodeCommand command(IdOf(model, "B"), aq::level::NodeParent{ IdOf(model, "A"), -1 }, STRUCTURE_APPEND);
			CheckUndoRedo(results, name + " MoveNode 別の親", model, command, "A(A1(A1a),A2,B),Ref|[Inline:S1][file:Other.level.json]");
		}

		// 移動: 同じ列の前へ([A, B, Ref] の B を 0 へ)
		{
			aq::level::LevelEditModel model;
			if (!LoadStructureTestModel(model)) { Report(results, name, false, "試験用 Level を読み込めない"); return; }
			editor::MoveNodeCommand command(IdOf(model, "B"), aq::level::NodeParent{ 0, -1 }, 0);
			CheckUndoRedo(results, name + " MoveNode 同じ列の前へ", model, command, "B,A(A1(A1a),A2),Ref|[Inline:S1][file:Other.level.json]");
		}

		// 移動: 同じ列の後ろへ([A, B, Ref] の A を、取り外した後の添字 2 へ)
		{
			aq::level::LevelEditModel model;
			if (!LoadStructureTestModel(model)) { Report(results, name, false, "試験用 Level を読み込めない"); return; }
			editor::MoveNodeCommand command(IdOf(model, "A"), aq::level::NodeParent{ 0, -1 }, 2);
			CheckUndoRedo(results, name + " MoveNode 同じ列の後ろへ", model, command, "B,Ref,A(A1(A1a),A2)|[Inline:S1][file:Other.level.json]");
		}

		// 名前の変更: 変更前はコンストラクタの値ではなく、実行の時点のモデルから取る
		{
			aq::level::LevelEditModel model;
			if (!LoadStructureTestModel(model)) { Report(results, name, false, "試験用 Level を読み込めない"); return; }
			editor::RenameNodeCommand command(IdOf(model, "B"), "NotTheOldName", "B2");
			CheckUndoRedo(results, name + " RenameNode", model, command, "A(A1(A1a),A2),B2,Ref|[Inline:S1][file:Other.level.json]");
		}
	}


	/** T13 (a): フィールドの変更・コンポーネントの追加と削除(削除は先頭と中ほどで、型の並びまで戻る) */
	void RunUndoRedoComponentTest(TestResults& results)
	{
		const std::string name = "T13 (a)";

		// フィールドの変更(実体ノード)
		{
			aq::level::LevelEditModel model;
			if (!LoadValueTestModel(model)) { Report(results, name, false, "試験用 Level を読み込めない"); return; }
			editor::SetFieldCommand command(IdOf(model, "InlineChild"), "Spawner", "interval", JsonValue(3.0));
			CheckUndoRedo(results, name + " SetField", model, command);
		}

		// コンポーネントの追加
		{
			aq::level::LevelEditModel model;
			if (!LoadValueTestModel(model)) { Report(results, name, false, "試験用 Level を読み込めない"); return; }
			editor::AddComponentCommand command(IdOf(model, "InlineChild"), "AudioListener");
			CheckUndoRedo(results, name + " AddComponent", model, command);
		}

		// コンポーネントの削除。並びを決めたノード(AudioListener, Transform, AudioSource, Spawner)を置いて、先頭と中ほどを外す
		struct RemoveCase
		{
			const char* label;
			const char* typeName;
		};
		const RemoveCase cases[] = {
			{ "先頭",   "AudioListener" },
			{ "中ほど", "AudioSource" },
		};
		for (const RemoveCase& c : cases) {
			const std::string caseName = name + " RemoveComponent " + c.label;

			aq::level::LevelEditModel model;
			if (!LoadStructureTestModel(model)) { Report(results, caseName, false, "試験用 Level を読み込めない"); return; }
			auto node  = std::make_unique<PrefabEditNode>();
			node->name = "Ordered";
			AddEditComponent(*node, "AudioListener");
			AddEditComponent(*node, "Transform");
			PatchEditComponent(AddEditComponent(*node, "AudioSource"), R"({"volume":0.25,"loop":true})");
			AddEditComponent(*node, "Spawner");
			const uint32_t id = model.InsertNode(aq::level::NodeParent{ 0, -1 }, STRUCTURE_APPEND, std::move(node));
			if (id == 0) { Report(results, caseName, false, "ノードを置けない"); continue; }

			const std::vector<size_t> originalOrder = ComponentTypeOrder(model, id);
			editor::RemoveComponentCommand command(id, c.typeName);
			CheckUndoRedo(results, caseName, model, command);

			// CheckUndoRedo の後はやり直した状態。取り消して、型の並びが元と一致するかを見る
			const bool undone  = command.Undo(model);
			const bool ordered = undone && ComponentTypeOrder(model, id) == originalOrder;
			Report(results, caseName + " 型の並び", ordered, ordered ? std::string() : "取り消した後のコンポーネントの並びが元と違う");
		}
	}


	/** T13 (b): 子のフィールドを変える → 親を削除 → 削除を取り消す → 子の変更を取り消す で、子の値と ToJson が元に戻る */
	void RunUndoRedoDescendantTest(TestResults& results)
	{
		const std::string name = "T13 (b) 削除の取り消しの後の子の取り消し";
		aq::level::LevelEditModel model;
		if (!LoadValueTestModel(model)) { Report(results, name, false, "試験用 Level を読み込めない"); return; }

		const uint32_t  idChild       = IdOf(model, "InlineChild");
		const uint32_t  idParent      = IdOf(model, "Inline");
		const JsonValue original      = model.ToJson();
		const double    originalValue = FieldAsDouble(model, idChild, "Spawner", "interval");

		editor::CommandHistory history;
		auto setField     = std::make_unique<editor::SetFieldCommand>(idChild, "Spawner", "interval", JsonValue(3.0));
		auto deleteParent = std::make_unique<editor::DeleteNodeCommand>(idParent);
		const bool executed = setField->Execute(model) && FieldAsDouble(model, idChild, "Spawner", "interval") == 3.0;
		history.Push(std::move(setField));
		const bool deleted = deleteParent->Execute(model) && model.FindNode(idChild) == nullptr;
		history.Push(std::move(deleteParent));
		ReportOnFailure(results, name + " 実行", executed && deleted, "値の変更か親の削除ができない");

		const bool undoneDelete = history.Undo(model) && model.FindNode(idChild) != nullptr;
		const bool undoneField  = history.Undo(model);
		const double value      = FieldAsDouble(model, idChild, "Spawner", "interval");
		ReportOnFailure(results, name + " 子の値", undoneDelete && undoneField && value == originalValue,
			"取り消した後の値: " + std::to_string(value) + " / 元: " + std::to_string(originalValue));
		const JsonValue after = model.ToJson();
		const bool      same  = JsonEqualsExact(original, after);
		ReportOnFailure(results, name + " ToJson", same, same ? std::string() : "元と違う: " + StringifyOneLine(after));
	}


	/** T13 (c): 参照ノードの値を変えて取り消すと、ToJson が元と一字一句一致する(override に無いキーは残らない) */
	void RunUndoRedoReferenceTest(TestResults& results)
	{
		const std::string name = "T13 (c) 参照ノード";

		struct ReferenceCase
		{
			const char* label;
			const char* nodeName;
			const char* typeName;
			const char* key;
			const char* valueText;
		};
		const ReferenceCase cases[] = {
			{ "override の無いコンポーネント", VALUE_REF_PLAIN_NAME, "Spawner",   "interval", "2" },
			{ "override に無いキー",           VALUE_REF_FULL_NAME,  "Transform", "scale",    "[2,2,2]" },
			{ "override にあるキー",           VALUE_REF_FULL_NAME,  "Spawner",   "interval", "7" },
		};
		for (const ReferenceCase& c : cases) {
			aq::level::LevelEditModel model;
			if (!LoadValueTestModel(model)) { ReportOnFailure(results, name, false, "試験用 Level を読み込めない"); return; }
			editor::SetFieldCommand command(IdOf(model, c.nodeName), c.typeName, c.key, aq::util::JsonParser::ParseString(c.valueText));
			CheckUndoRedo(results, name + " " + c.label, model, command);
		}
	}


	/** T13 (d): 同じキーへの SetField を 2 つ続けて実行し、逆順に取り消すと元に戻る(変更前を実行の時点で取る) */
	void RunUndoRedoSameKeyTest(TestResults& results)
	{
		const std::string name = "T13 (d) 同じキーへの 2 回の変更";

		struct SameKeyCase
		{
			const char* label;
			const char* nodeName;
			const char* typeName;
			const char* key;
			double      first;
			double      second;
		};
		const SameKeyCase cases[] = {
			{ "実体ノード", "InlineChild",        "Spawner", "interval", 3.0, 4.0 },
			{ "参照ノード", VALUE_REF_PLAIN_NAME, "Spawner", "maxCount", 5.0, 7.0 },
		};
		for (const SameKeyCase& c : cases) {
			const std::string caseName = name + " " + c.label;
			aq::level::LevelEditModel model;
			if (!LoadValueTestModel(model)) { ReportOnFailure(results, caseName, false, "試験用 Level を読み込めない"); return; }

			const uint32_t  id            = IdOf(model, c.nodeName);
			const JsonValue original      = model.ToJson();
			const double    originalValue = FieldAsDouble(model, id, c.typeName, c.key);

			// インスペクターが同じフレームに 2 回積んだときと同じく、2 つとも作ってから順に実行する
			editor::SetFieldCommand first(id, c.typeName, c.key, JsonValue(c.first));
			editor::SetFieldCommand second(id, c.typeName, c.key, JsonValue(c.second));
			const bool executed = first.Execute(model) && second.Execute(model)
				&& FieldAsDouble(model, id, c.typeName, c.key) == c.second;
			ReportOnFailure(results, caseName + " 実行", executed, "実行できない / 値が違う");

			const bool   undoneSecond = second.Undo(model);
			const double middle       = FieldAsDouble(model, id, c.typeName, c.key);
			ReportOnFailure(results, caseName + " 2 つ目の取り消し", undoneSecond && middle == c.first,
				"取り消した後の値: " + std::to_string(middle) + " / 期待: " + std::to_string(c.first));

			const bool   undoneFirst = first.Undo(model);
			const double restored    = FieldAsDouble(model, id, c.typeName, c.key);
			const bool   same        = JsonEqualsExact(original, model.ToJson());
			ReportOnFailure(results, caseName + " 1 つ目の取り消し", undoneFirst && restored == originalValue && same,
				"取り消した後の値: " + std::to_string(restored) + " / 元: " + std::to_string(originalValue)
				+ (same ? std::string() : " / ToJson が元と違う"));
		}
	}


	/** T13 (e): CommandHistory の列の扱いと、保存の印 */
	void RunCommandHistoryTest(TestResults& results)
	{
		const std::string         name = "T13 (e) CommandHistory";
		aq::level::LevelEditModel model;

		// Push で Redo の列が消える
		{
			editor::CommandHistory history;
			const bool empty = !history.CanUndo() && !history.CanRedo() && history.GetUndoName() == nullptr && history.GetRedoName() == nullptr;
			PushHistoryTestCommand(history, model);
			PushHistoryTestCommand(history, model);
			const bool undone   = history.Undo(model);
			const bool hasRedo  = history.CanRedo() && history.GetRedoCount() == 1 && history.GetRedoName() != nullptr
				&& std::string(history.GetRedoName()) == "History Test";
			PushHistoryTestCommand(history, model);
			const bool redoGone = !history.CanRedo() && history.GetRedoCount() == 0 && history.GetUndoCount() == 2;
			ReportOnFailure(results, name + " Push で Redo が消える", empty && undone && hasRedo && redoGone,
				"undo=" + std::to_string(history.GetUndoCount()) + " redo=" + std::to_string(history.GetRedoCount()));
		}

		// 上限を超えたら古いものから消える
		{
			editor::CommandHistory history;
			for (size_t i = 0; i < editor::CommandHistory::MAX_ENTRIES + 10; ++i) {
				PushHistoryTestCommand(history, model);
			}
			const bool capped = history.GetUndoCount() == editor::CommandHistory::MAX_ENTRIES;
			size_t undoCount = 0;
			while (history.Undo(model)) ++undoCount;
			ReportOnFailure(results, name + " 上限", capped && undoCount == editor::CommandHistory::MAX_ENTRIES && !history.CanUndo(),
				"積んだ後 " + std::to_string(editor::CommandHistory::MAX_ENTRIES) + " 件のはずが取り消せたのは " + std::to_string(undoCount) + " 件");
		}

		// 保存の印: 保存 → Undo で dirty → Redo で dirty でない → Undo して新しいコマンドで以後ずっと dirty
		{
			editor::CommandHistory history;
			const bool cleanAtStart = history.IsAtSavedState();
			PushHistoryTestCommand(history, model);
			const bool dirtyAfterPush = !history.IsAtSavedState();
			history.MarkSaved();
			const bool cleanAfterSave = history.IsAtSavedState();
			history.Undo(model);
			const bool dirtyAfterUndo = !history.IsAtSavedState();
			history.Redo(model);
			const bool cleanAfterRedo = history.IsAtSavedState();
			history.Undo(model);
			PushHistoryTestCommand(history, model);
			bool alwaysDirty = !history.IsAtSavedState();
			history.Undo(model);
			alwaysDirty = alwaysDirty && !history.IsAtSavedState();
			history.Redo(model);
			alwaysDirty = alwaysDirty && !history.IsAtSavedState();
			ReportOnFailure(results, name + " 保存の印", cleanAtStart && dirtyAfterPush && cleanAfterSave && dirtyAfterUndo && cleanAfterRedo && alwaysDirty,
				std::string("start ") + (cleanAtStart ? "clean" : "dirty") + " / push " + (dirtyAfterPush ? "dirty" : "clean")
				+ " / save " + (cleanAfterSave ? "clean" : "dirty") + " / undo " + (dirtyAfterUndo ? "dirty" : "clean")
				+ " / redo " + (cleanAfterRedo ? "clean" : "dirty") + " / 新しいコマンドの後 " + (alwaysDirty ? "dirty" : "clean"));
		}

		// 保存済みの空から上限 + 1 回積んで上限回取り消しても、捨てた項目の後の状態なので dirty のまま
		{
			editor::CommandHistory history;
			history.MarkCleanEmpty();
			for (size_t i = 0; i < editor::CommandHistory::MAX_ENTRIES + 1; ++i) {
				PushHistoryTestCommand(history, model);
			}
			for (size_t i = 0; i < editor::CommandHistory::MAX_ENTRIES; ++i) {
				history.Undo(model);
			}
			ReportOnFailure(results, name + " 上限で捨てた後の保存の印", !history.CanUndo() && !history.IsAtSavedState(),
				"上限 + 1 回積んで上限回取り消したら、保存済みの状態と一致した");
		}

		// 失敗する Undo は false を返し、履歴が空になり、保存の印は一致しなくなる
		{
			editor::CommandHistory history;
			PushHistoryTestCommand(history, model);
			PushHistoryTestCommand(history, model, true, false);
			history.MarkSaved();
			const bool failed = !history.Undo(model);
			ReportOnFailure(results, name + " Undo の失敗", failed && !history.CanUndo() && !history.CanRedo() && !history.IsAtSavedState(),
				"undo=" + std::to_string(history.GetUndoCount()) + " redo=" + std::to_string(history.GetRedoCount()));
		}

		// 失敗する Redo も同じ
		{
			editor::CommandHistory history;
			PushHistoryTestCommand(history, model);
			PushHistoryTestCommand(history, model, false, true);
			history.Undo(model);
			const bool failed = !history.Redo(model);
			ReportOnFailure(results, name + " Redo の失敗", failed && !history.CanUndo() && !history.CanRedo() && !history.IsAtSavedState(),
				"undo=" + std::to_string(history.GetUndoCount()) + " redo=" + std::to_string(history.GetRedoCount()));
		}
	}


	/**
	 * T13 Undo / Redo
	 *
	 * Editor のコマンド(Commands/)と CommandHistory を直接コンパイルして、T10 / T11 の試験用 Level で
	 * 取り消しとやり直しを試す。画面も Engine も作らない。
	 */
	void RunUndoRedoTests(TestResults& results)
	{
		RunUndoRedoNodeTest(results);
		RunUndoRedoComponentTest(results);
		RunUndoRedoDescendantTest(results);
		RunUndoRedoReferenceTest(results);
		RunUndoRedoSameKeyTest(results);
		RunCommandHistoryTest(results);
	}
#endif


	/** T12 (a): ExtractLeadingComments が、ルートの値より前の // の行と空行だけを LF にそろえて返す */
	void RunLeadingCommentExtractTest(TestResults& results)
	{
		const std::string name = "T12 (a) ExtractLeadingComments";
		struct Case
		{
			const char* label;
			std::string text;
			std::string expected;
		};
		const Case cases[] = {
			{ "コメント 2 行",       "// a\n// b\n{\"x\": 1}\n",                       "// a\n// b\n" },
			{ "CRLF",               "// a\r\n// b\r\n{\"x\": 1}\r\n",                 "// a\n// b\n" },
			{ "BOM",                "\xEF\xBB\xBF// a\n{}",                           "// a\n" },
			{ "途中の空行を保つ",     "// a\n\n// b\n{}",                              "// a\n\n// b\n" },
			{ "後ろの空行は落とす",   "// a\n\n\n{}",                                  "// a\n" },
			{ "字下げしたコメント",   "  // a\n{}",                                    "  // a\n" },
			{ "コメントなし",         "{\"x\": 1}",                                    "" },
			{ "空行だけ",             "\n\n{}",                                        "" },
			{ "値の中のコメントは含まない", "{\n  // inner\n  \"x\": 1\n}",              "" },
			{ "ファイルの終わりまでコメント", "// a\n// b",                              "// a\n// b\n" },
		};
		for (const Case& c : cases) {
			const std::string actual = aq::util::JsonParser::ExtractLeadingComments(c.text);
			Report(results, name + " " + c.label, actual == c.expected,
				"期待 [" + c.expected + "] / 実際 [" + actual + "]");
		}
	}


#ifdef AQ_DEBUG_IMGUI
	/** ファイルの中身を全部読む(読めなければ空) */
	std::string ReadWholeFile(const std::string& path)
	{
		std::ifstream ifs(path, std::ios::binary);
		if (!ifs.is_open()) return std::string();
		return std::string(std::istreambuf_iterator<char>(ifs), std::istreambuf_iterator<char>());
	}


	/**
	 * T12 (b): LevelEditModel が開いたファイルの先頭のコメントを保存で書き戻す。
	 * 値は今までどおり書き出され、新規の Level にはコメントが付かない
	 */
	void RunLeadingCommentSaveTest(TestResults& results)
	{
		namespace fs = std::filesystem;

		const std::string name      = "T12 (b) 先頭コメントの保存";
		const std::string levelPath = std::string(TEST_DATA_DIR) + "/" + OVERRIDE_TEST_LEVEL;
		const std::string expected  = aq::util::JsonParser::ExtractLeadingComments(ReadWholeFile(levelPath));
		if (expected.empty()) {
			Report(results, name, false, "試験用 Level に先頭のコメントが無い: " + levelPath);
			return;
		}

		std::error_code ec;
		const fs::path tempPath = fs::temp_directory_path(ec) / "ReflectionTest_T12.level.json";

		aq::level::LevelEditModel model;
		const bool loaded = model.LoadFile(levelPath);
		Report(results, name + " 読み込みで控える", loaded && model.GetLeadingComments() == expected,
			"控えた [" + model.GetLeadingComments() + "]");

		const bool saved   = loaded && model.SaveFile(tempPath.generic_string());
		const std::string written = ReadWholeFile(tempPath.string());
		const bool hasHeader = saved && written.compare(0, expected.size(), expected) == 0;
		Report(results, name + " 先頭に書き戻す", hasHeader, saved ? "書いたファイルの先頭: " + written.substr(0, 200) : "保存できない");

		// コメントの後ろは今までどおりの JSON(読み直して ToJson と一致する)
		const JsonValue reread = aq::util::JsonParser::ParseFile(tempPath.string().c_str());
		Report(results, name + " 値は変わらない", saved && JsonEqualsExact(reread, model.ToJson()),
			"読み直した値: " + StringifyOneLine(reread));

		// 読み直して保存し直してもコメントが増えない
		aq::level::LevelEditModel again;
		const bool reloaded = again.LoadFile(tempPath.generic_string()) && again.SaveFile(tempPath.generic_string());
		const std::string rewritten = ReadWholeFile(tempPath.string());
		Report(results, name + " 保存し直しても増えない", reloaded && rewritten == written,
			"1 回目と 2 回目で中身が違う");

		// 新規の Level にはコメントが付かない
		model.NewLevel();
		const bool newSaved = model.SaveFile(tempPath.generic_string());
		const std::string fresh = ReadWholeFile(tempPath.string());
		Report(results, name + " 新規は付かない", newSaved && !fresh.empty() && fresh[0] == '{',
			"新規の先頭: " + fresh.substr(0, 80));

		fs::remove(tempPath, ec);
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


	/**
	 * T11 (g): 値を変えた Level を一時ファイルへ SaveFile し、LevelSerializer で読んで Load → Flush すると、
	 * エンティティが作られ、変えた値が実体のコンポーネント(serialize)に出る
	 */
	void RunValueSaveLoadTest(TestResults& results)
	{
		namespace fs = std::filesystem;

		const std::string name = "T11 (g) 保存した Level の生成";
		const std::string key  = "<t11-g>";

		// 参照ノードは、保存先が一時ディレクトリでも指す先が変わらないよう、TestData の Prefab を絶対パスで指す
		std::error_code ec;
		const std::string levelPath  = (fs::temp_directory_path(ec) / "ReflectionTest_T11_Value.level.json").generic_string();
		const std::string prefabPath = (fs::absolute(fs::path(TEST_DATA_DIR), ec) / "T6_Base.prefab.json").generic_string();

		JsonValue plainComponents = JsonValue::MakeObject();
		plainComponents.Set("Transform", JsonValue::MakeObject());
		JsonValue plain = JsonValue::MakeObject();
		plain.Set("name", JsonValue(std::string("Plain")));
		plain.Set("components", std::move(plainComponents));
		JsonValue ref = JsonValue::MakeObject();
		ref.Set("name", JsonValue(std::string("Ref")));
		ref.Set("prefab", JsonValue(prefabPath));
		JsonValue entities = JsonValue::MakeArray();
		entities.PushBack(std::move(plain));
		entities.PushBack(std::move(ref));
		JsonValue levelJson = JsonValue::MakeObject();
		levelJson.Set("name", JsonValue(std::string("T11_G")));
		levelJson.Set("entities", std::move(entities));

		aq::level::LevelEditModel model;
		if (!model.FromJson(levelJson)) { Report(results, name, false, "編集モデルへ読み込めない"); return; }

		// 実体ノードの Vector3 と、参照ノードの override の無いコンポーネントの値を変える
		const bool edited =
			model.SetComponentField(IdOf(model, "Plain"), "Transform", "position", aq::util::JsonParser::ParseString("[4,5,6]"))
			&& model.SetComponentField(IdOf(model, "Ref"), "Spawner", "maxCount", JsonValue(7.0));
		if (!edited) { Report(results, name, false, "値を変えられない"); return; }

		if (!model.SaveFile(levelPath)) { Report(results, name, false, "一時ファイルへ保存できない: " + levelPath); return; }

		// 保存した参照ノードには、変えたキーだけが書かれる
		const JsonValue saved = aq::util::JsonParser::ParseFile(levelPath.c_str());
		const JsonValue expectedOverrides = aq::util::JsonParser::ParseString(R"({"components":{"Spawner":{"maxCount":7}}})");
		ReportNodeEquals(results, name + " 保存した差分", OverridesOf(saved["entities"][1]), expectedOverrides);

		// LevelSerializer で読んで生成する
		std::shared_ptr<const aq::level::LevelData> data = aq::level::LevelSerializer::Load(levelPath.c_str());
		if (!data) {
			Report(results, name, false, "保存した Level を LevelSerializer で読めない");
			fs::remove(fs::path(levelPath), ec);
			return;
		}
		LevelRegistry::Get().Register(key, data);

		LevelManager&           levelManager = LevelManager::Get();
		aq::ecs::EntityContext& ctx          = aq::ecs::EntityContext::Get();
		const LevelId           levelId      = levelManager.Load(key);
		ctx.FlushPendingCommands();
		const bool spawned = levelId.IsValid() && levelManager.IsSpawned(levelId);

		std::vector<aq::ecs::EntityHandle> handles;
		aq::ecs::Foreach<aq::ecs::TransformComponent>([&handles](const aq::ecs::Entity& entity, aq::ecs::TransformComponent*)
			{
				handles.push_back(entity.GetHandle());
			});

		const ComponentRegistry& registry      = ComponentRegistry::Get();
		const ComponentMeta*     transformMeta = registry.Find("Transform");
		const ComponentMeta*     spawnerMeta   = registry.Find("Spawner");
		bool positionFound = false;
		bool maxCountFound = false;
		for (const aq::ecs::EntityHandle& handle : handles) {
			if (transformMeta && transformMeta->serialize) {
				JsonValue transform;
				transformMeta->serialize(handle, transform);
				const JsonValue& position = transform["position"];
				if (position.Size() == 3 && position[0].AsDouble() == 4.0 && position[1].AsDouble() == 5.0 && position[2].AsDouble() == 6.0) {
					positionFound = true;
				}
			}
			if (spawnerMeta && spawnerMeta->has && spawnerMeta->serialize && spawnerMeta->has(handle)) {
				JsonValue spawner;
				spawnerMeta->serialize(handle, spawner);
				if (spawner["maxCount"].AsDouble() == 7.0) { maxCountFound = true; }
			}
		}

		const bool passed = spawned && handles.size() >= 2 && positionFound && maxCountFound;
		Report(results, name, passed, passed ? std::string()
			: std::string("IsSpawned ") + (spawned ? "true" : "false") + " / エンティティ数 " + std::to_string(handles.size())
			  + " / Transform.position " + (positionFound ? "あり" : "なし") + " / Spawner.maxCount 7 " + (maxCountFound ? "あり" : "なし"));

		// 後始末(Level と登録と一時ファイルを外す)
		ResetSpawnTestWorld();
		LevelRegistry::Get().Unregister(key);
		fs::remove(fs::path(levelPath), ec);
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
		RunValueSaveLoadTest(results);
#else
		std::printf("        T8 (f) / T11 (g): AQ_DEBUG_IMGUI が無い構成(Release)なので行わない(LevelEditModel が無い)\n");
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


	/** T14 (a) で受け取った文字列(どのスレッドからも来るのでミューテックスの下で控える) */
	std::mutex               g_logListenerMutex;
	std::vector<std::string> g_logListenerReceived;


	/** T14 (a) で登録する関数 */
	void LogListenerForTest(const char* text)
	{
		std::lock_guard<std::mutex> lock(g_logListenerMutex);
		g_logListenerReceived.push_back(text);
	}


	/** T14 (a) で受け取った中に marker を含むものがあるか(あれば outText にその文字列) */
	bool FindReceivedLog(const std::string& marker, std::string* outText)
	{
		std::lock_guard<std::mutex> lock(g_logListenerMutex);
		for (const std::string& text : g_logListenerReceived) {
			if (text.find(marker) == std::string::npos) continue;
			if (outText != nullptr) {
				*outText = text;
			}
			return true;
		}
		return false;
	}


	/** T14 (a): SetLogListener で登録した関数に StartupLog と EnginePrintf(Debug だけ)が届き、外すと届かない */
	void RunLogListenerTest(TestResults& results)
	{
		const std::string name = "T14 (a)";
		{
			std::lock_guard<std::mutex> lock(g_logListenerMutex);
			g_logListenerReceived.clear();
		}
		aq::debug::SetLogListener(&LogListenerForTest);

		// StartupLog は StartupMark で整形した 1 行("[startup] 経過ms (+差分) 本文")で届く
		aq::StartupLog("T14 startup marker");
		std::string received;
		const bool startupReceived  = FindReceivedLog("T14 startup marker", &received);
		const bool startupFormatted = startupReceived && received.rfind("[startup] ", 0) == 0;
		Report(results, name + " StartupLog が整形した 1 行で届く", startupFormatted,
			startupFormatted ? std::string() : "受け取った [" + received + "]");

		// EnginePrintf は Debug だけ出る(Release では式ごと消える)
		EnginePrintf("T14 printf marker %d\n", 1);
		const bool printfReceived = FindReceivedLog("T14 printf marker 1", nullptr);
#ifdef _DEBUG
		Report(results, name + " EnginePrintf が Debug で届く", printfReceived, printfReceived ? std::string() : "届かない");
#else
		Report(results, name + " EnginePrintf が Release で届かない", !printfReceived, printfReceived ? "届いた" : std::string());
#endif

		aq::debug::SetLogListener(nullptr);
		aq::StartupLog("T14 detached marker");
		const bool detachedReceived = FindReceivedLog("T14 detached marker", nullptr);
		Report(results, name + " nullptr で外すと届かない", !detachedReceived, detachedReceived ? "外した後に届いた" : std::string());
	}


	/** 行の列を 1 行の文字列にする(失敗の詳細表示用) */
	std::string JoinLinesForDetail(const std::vector<std::string>& lines)
	{
		std::string out = "[";
		for (size_t i = 0; i < lines.size(); ++i) {
			if (i > 0) out += ", ";
			out += "\"" + lines[i] + "\"";
		}
		out += "]";
		return out;
	}


	/** 重要度の表示名(失敗の詳細表示用) */
	const char* SeverityName(const editor::LogSeverity severity)
	{
		switch (severity) {
		case editor::LogSeverity::Info:    return "Info";
		case editor::LogSeverity::Warning: return "Warning";
		case editor::LogSeverity::Error:   return "Error";
		}
		return "?";
	}


	/** T14 (b): ConsoleLog の行の分割・重要度の推定・上限・CopySince・Clear・Attach と Detach */
	void RunConsoleLogTest(TestResults& results)
	{
		const std::string name = "T14 (b) ConsoleLog";

		// 改行での分割(\r を落とし、末尾の空行を落とし、途中の空行は残す)
		{
			struct Case
			{
				const char*              label;
				const char*              text;
				std::vector<std::string> expected;
			};
			const Case cases[] = {
				{ "LF",                 "a\nb",       { "a", "b" } },
				{ "CRLF",               "a\r\nb\r\n", { "a", "b" } },
				{ "末尾の空行を落とす", "a\n\n\n",    { "a" } },
				{ "途中の空行は残す",   "a\n\nb",     { "a", "", "b" } },
				{ "改行なし",           "a",          { "a" } },
				{ "空",                 "",           {} },
				{ "改行だけ",           "\r\n",       {} },
			};
			for (const Case& c : cases) {
				std::vector<std::string> actual;
				editor::ConsoleLog::SplitLines(c.text, actual);
				const bool passed = actual == c.expected;
				Report(results, name + " 分割 " + c.label, passed,
					passed ? std::string() : "期待 " + JoinLinesForDetail(c.expected) + " / 実際 " + JoinLinesForDetail(actual));
			}
		}

		// 重要度の推定(英字は小文字にして探す)
		{
			struct Case
			{
				const char*         text;
				editor::LogSeverity expected;
			};
			const Case cases[] = {
				{ "Shader compile ERROR: x",                    editor::LogSeverity::Error },
				{ "[editor] open failed: missing.level.json",   editor::LogSeverity::Error },
				{ "Assertion failed: expr",                     editor::LogSeverity::Error },
				{ "[crash] exception 0xC0000005",               editor::LogSeverity::Error },
				{ "読み込みエラー",                             editor::LogSeverity::Error },
				{ "保存に失敗しました",                         editor::LogSeverity::Error },
				{ "WARNING: low memory",                        editor::LogSeverity::Warning },
				{ "[vk] Warn: x",                               editor::LogSeverity::Warning },
				{ "警告: 何か",                                 editor::LogSeverity::Warning },
				{ "[startup]     12.0 ms  (+    1.0)  WinMain", editor::LogSeverity::Info },
				{ "loaded 3 entities",                          editor::LogSeverity::Info },
				{ "",                                           editor::LogSeverity::Info },
			};
			for (const Case& c : cases) {
				const editor::LogSeverity actual = editor::ConsoleLog::EstimateSeverity(c.text);
				Report(results, name + " 重要度 \"" + c.text + "\"", actual == c.expected,
					actual == c.expected ? std::string() : std::string("期待 ") + SeverityName(c.expected) + " / 実際 " + SeverityName(actual));
			}
		}

		// Append は行ごとに通し番号と重要度を付ける
		{
			editor::ConsoleLog log;
			log.Append("info line\nwarning line\r\nerror line\n\n");
			std::vector<editor::ConsoleLine> lines;
			const uint64_t last = log.CopySince(0, lines);
			const bool passed = lines.size() == 3 && last == 3
				&& lines[0].serial == 1 && lines[0].text == "info line"    && lines[0].severity == editor::LogSeverity::Info
				&& lines[1].serial == 2 && lines[1].text == "warning line" && lines[1].severity == editor::LogSeverity::Warning
				&& lines[2].serial == 3 && lines[2].text == "error line"   && lines[2].severity == editor::LogSeverity::Error;
			Report(results, name + " Append", passed,
				passed ? std::string() : "行数 " + std::to_string(lines.size()) + " / 最後の番号 " + std::to_string(last));
		}

		// 上限を超えたら古いものから消える
		{
			static constexpr size_t OVERFLOW_COUNT = 10;
			editor::ConsoleLog log;
			for (size_t i = 0; i < editor::ConsoleLog::MAX_LINES + OVERFLOW_COUNT; ++i) {
				log.Append(("line " + std::to_string(i)).c_str());
			}
			std::vector<editor::ConsoleLine> lines;
			const uint64_t last = log.CopySince(0, lines);
			bool contiguous = lines.size() == editor::ConsoleLog::MAX_LINES;
			for (size_t i = 0; contiguous && i < lines.size(); ++i) {
				contiguous = lines[i].serial == OVERFLOW_COUNT + 1 + i && lines[i].text == "line " + std::to_string(OVERFLOW_COUNT + i);
			}
			const bool passed = log.GetLineCount() == editor::ConsoleLog::MAX_LINES && contiguous
				&& last == editor::ConsoleLog::MAX_LINES + OVERFLOW_COUNT;
			Report(results, name + " 上限 " + std::to_string(editor::ConsoleLog::MAX_LINES) + " 行", passed,
				passed ? std::string() : "行数 " + std::to_string(lines.size()) + " / 最後の番号 " + std::to_string(last)
					+ (lines.empty() ? std::string() : " / 先頭 [" + lines.front().text + "]"));
		}

		// CopySince は新しい行だけを返し、Clear は通し番号を戻さない
		{
			editor::ConsoleLog log;
			log.Append("a\nb");
			std::vector<editor::ConsoleLine> first;
			const uint64_t afterFirst = log.CopySince(0, first);
			log.Append("c");
			std::vector<editor::ConsoleLine> second;
			const uint64_t afterSecond = log.CopySince(afterFirst, second);
			std::vector<editor::ConsoleLine> none;
			const uint64_t afterNone = log.CopySince(afterSecond, none);
			const bool sincePassed = first.size() == 2 && afterFirst == 2
				&& second.size() == 1 && second[0].text == "c" && second[0].serial == 3 && afterSecond == 3
				&& none.empty() && afterNone == 3;
			Report(results, name + " CopySince が新しい行だけを返す", sincePassed,
				sincePassed ? std::string() : "1 回目 " + std::to_string(first.size()) + " 行 / 2 回目 " + std::to_string(second.size())
					+ " 行 / 3 回目 " + std::to_string(none.size()) + " 行");

			log.Clear();
			std::vector<editor::ConsoleLine> cleared;
			const uint64_t afterClear = log.CopySince(0, cleared);
			log.Append("d");
			std::vector<editor::ConsoleLine> appended;
			log.CopySince(afterSecond, appended);
			const bool clearPassed = cleared.empty() && afterClear == 0 && log.GetLineCount() == 1
				&& appended.size() == 1 && appended[0].text == "d" && appended[0].serial == 4;
			Report(results, name + " Clear", clearPassed,
				clearPassed ? std::string() : "Clear の後に " + std::to_string(cleared.size()) + " 行 / 足した後に " + std::to_string(appended.size()) + " 行");
		}

		// Attach で NotifyLogListener が届き、Detach で届かなくなる
		{
			editor::ConsoleLog log;
			log.Attach();
			aq::debug::NotifyLogListener("attached line\n");
			log.Detach();
			aq::debug::NotifyLogListener("detached line\n");
			std::vector<editor::ConsoleLine> lines;
			log.CopySince(0, lines);
			const bool passed = lines.size() == 1 && lines[0].text == "attached line";
			Report(results, name + " Attach と Detach", passed, passed ? std::string() : "行数 " + std::to_string(lines.size()));
		}
	}


	/** T14 (c): 4 スレッドから同時に 1000 行ずつ送り、行数と各スレッドの順序が保たれる */
	void RunConsoleLogThreadTest(TestResults& results)
	{
		static constexpr int THREAD_COUNT     = 4;
		static constexpr int LINES_PER_THREAD = 1000;
		static_assert(static_cast<size_t>(THREAD_COUNT * LINES_PER_THREAD) <= editor::ConsoleLog::MAX_LINES,
			"上限を超えると古い行が消えて数えられない");

		const std::string  name = "T14 (c) 複数スレッド";
		editor::ConsoleLog log;
		log.Attach();
		{
			std::vector<std::thread> threads;
			for (int t = 0; t < THREAD_COUNT; ++t) {
				threads.emplace_back([t]()
					{
						char text[64];
						for (int i = 0; i < LINES_PER_THREAD; ++i) {
							std::snprintf(text, sizeof(text), "T14c %d %d\n", t, i);
							aq::debug::NotifyLogListener(text);
						}
					});
			}
			for (std::thread& thread : threads) {
				thread.join();
			}
		}
		log.Detach();

		std::vector<editor::ConsoleLine> lines;
		log.CopySince(0, lines);

		// 各スレッドの行が 0 から順に並んでいるか(ほかのログが混ざっても数えない)
		std::array<int, THREAD_COUNT> nextIndex = {};
		size_t testLineCount = 0;
		bool   ordered       = true;
		for (const editor::ConsoleLine& line : lines) {
			int thread = -1;
			int index  = -1;
			if (std::sscanf(line.text.c_str(), "T14c %d %d", &thread, &index) != 2) continue;
			++testLineCount;
			if (thread < 0 || thread >= THREAD_COUNT || index != nextIndex[static_cast<size_t>(thread)]) {
				ordered = false;
				continue;
			}
			++nextIndex[static_cast<size_t>(thread)];
		}
		bool complete = true;
		for (const int count : nextIndex) {
			complete = complete && count == LINES_PER_THREAD;
		}

		const bool passed = testLineCount == static_cast<size_t>(THREAD_COUNT * LINES_PER_THREAD) && ordered && complete;
		Report(results, name, passed,
			passed ? std::string() : "行数 " + std::to_string(testLineCount) + " / 順序 " + (ordered ? "OK" : "NG"));
	}


	/**
	 * T14 ログの通知
	 *
	 * エンジンの SetLogListener と、Editor の ConsoleLog(ConsoleLog.cpp を直接コンパイルする)を試す。
	 * 画面も Engine も作らない。登録した関数は最後に必ず外す(後のログが試験用の関数へ流れないように)
	 */
	void RunLogTests(TestResults& results)
	{
		RunLogListenerTest(results);
		RunConsoleLogTest(results);
		RunConsoleLogThreadTest(results);
		aq::debug::SetLogListener(nullptr);
	}


	/** T15 (a) ClassifyAsset: 拡張子の大文字小文字、二重拡張子、フォルダを含むパス、種類にならないもの */
	void RunAssetClassifyTest(TestResults& results)
	{
		using aq::ecs::AssetKind;

		struct Case
		{
			const char* path;
			AssetKind   expected;
		};
		const Case cases[] = {
			{ "Assets/Character/Character.fbx",    AssetKind::Model },
			{ "Assets/Old/Stage.tkm",              AssetKind::Model },
			{ "Assets/Tex/Wall.DDS",               AssetKind::Texture },
			{ "Assets/Tex/Wall.png",               AssetKind::Texture },
			{ "Assets/Tex/Photo.jpeg",             AssetKind::Texture },
			{ "Assets/Tex/Photo.jpg",              AssetKind::Texture },
			{ "Assets/Tex/Normal.tga",             AssetKind::Texture },
			{ "Assets/Prefabs/Enemy.Prefab.JSON",  AssetKind::Prefab },
			{ "Assets/Levels/Stage.level.json",    AssetKind::Level },
			{ "Assets/Fx/Fire.particle",           AssetKind::Particle },
			{ "Assets/Sound/Hit.wav",              AssetKind::Audio },
			{ "Assets/Sound/Bgm.OGG",              AssetKind::Audio },
			{ "Assets/Data/config.json",           AssetKind::None },
			{ "Assets/Data/README",                AssetKind::None },
			{ "Assets/dir.png/readme",             AssetKind::None },
		};

		for (const Case& c : cases) {
			const AssetKind actual = editor::AssetCatalog::ClassifyAsset(c.path);
			Report(results, std::string("T15 (a) ClassifyAsset ") + c.path, actual == c.expected,
				actual == c.expected ? std::string() : "種類 " + std::to_string(static_cast<int>(actual)));
		}

		struct TagCase
		{
			AssetKind   kind;
			const char* tag;
		};
		const TagCase tagCases[] = {
			{ AssetKind::Model, "[M]" }, { AssetKind::Texture, "[T]" }, { AssetKind::Prefab, "[P]" },
			{ AssetKind::Level, "[L]" }, { AssetKind::Particle, "[FX]" }, { AssetKind::Audio, "[A]" },
			{ AssetKind::None, "" },
		};
		bool tagsPassed = true;
		for (const TagCase& c : tagCases) {
			if (std::string(editor::AssetCatalog::GetKindTag(c.kind)) != c.tag) tagsPassed = false;
		}
		Report(results, "T15 (a) GetKindTag", tagsPassed, std::string());
	}


	/** フォルダの子の名前を ',' でつないだ文字列(試験の比較用) */
	std::string JoinFolderNames(const editor::AssetFolder& folder)
	{
		std::string text;
		for (const editor::AssetFolder& child : folder.folders) {
			if (!text.empty()) text += ",";
			text += child.name;
		}
		return text;
	}


	/** フォルダのファイルを「名前:種類のタグ」で ',' でつないだ文字列(試験の比較用。タグが空なら名前だけ) */
	std::string JoinFileNames(const editor::AssetFolder& folder)
	{
		std::string text;
		for (const editor::AssetEntry& file : folder.files) {
			if (!text.empty()) text += ",";
			text += file.name;
			const std::string tag = editor::AssetCatalog::GetKindTag(file.kind);
			if (!tag.empty()) text += ":" + tag;
		}
		return text;
	}


	/** 実際の文字列が期待と同じか確かめ、違えば detail に残す */
	bool ExpectText(std::string& detail, const char* label, const std::string& actual, const std::string& expected)
	{
		if (actual == expected) return true;
		detail += std::string(label) + " 期待 [" + expected + "] 実際 [" + actual + "] ";
		return false;
	}


	/** T15 (a) AssetCatalog: TestData/T15_Assets の木を読み、木の形・並び・種類・隠しファイル・FindFolder を確かめる */
	void RunAssetCatalogTest(TestResults& results)
	{
		const std::string name = "T15 (a) AssetCatalog";
		const std::string root = std::string(TEST_DATA_DIR) + "/" + T15_ASSETS_DIR;

		editor::AssetCatalog catalog(root);
		const bool wasLoadedBefore = catalog.IsLoaded();
		const bool refreshed       = catalog.Refresh();

		std::string detail;
		bool        passed = true;

		if (wasLoadedBefore || !refreshed || !catalog.IsLoaded()) {
			detail += "Refresh / IsLoaded ";
			passed = false;
		}
		passed &= ExpectText(detail, "根のパス", catalog.GetRoot(), root);

		const editor::AssetFolder& tree = catalog.GetTree();
		passed &= ExpectText(detail, "根の path", tree.path, root);

		// 根: フォルダは misc, Models, Textures(大文字小文字を無視した順)。.hidden_dir は出ない
		passed &= ExpectText(detail, "根のフォルダ", JoinFolderNames(tree), "misc,Models,Textures");
		// 根のファイル: .hidden は出ない。None の Note.txt は一覧に出る
		passed &= ExpectText(detail, "根のファイル", JoinFileNames(tree),
			"Enemy.prefab.json:[P],Fire.particle:[FX],Hit.wav:[A],Note.txt,Stage.level.json:[L]");

		const editor::AssetFolder* models   = catalog.FindFolder(root + "/Models");
		const editor::AssetFolder* sub      = catalog.FindFolder(root + "/Models/Sub");
		const editor::AssetFolder* misc     = catalog.FindFolder(root + "/misc");
		const editor::AssetFolder* textures = catalog.FindFolder(root + "/Textures");
		if (models == nullptr || sub == nullptr || misc == nullptr || textures == nullptr) {
			detail += "FindFolder が見つからない ";
			passed = false;
		} else {
			passed &= ExpectText(detail, "Models のフォルダ", JoinFolderNames(*models), "Sub");
			passed &= ExpectText(detail, "Models のファイル", JoinFileNames(*models), "Hero.fbx:[M]");
			passed &= ExpectText(detail, "Sub のファイル", JoinFileNames(*sub), "Old.tkm:[M]");
			passed &= ExpectText(detail, "misc のファイル", JoinFileNames(*misc), "Beep.ogg:[A]");
			// A.png と b.png の並び(大文字小文字を無視)と、大文字の拡張子 .DDS
			passed &= ExpectText(detail, "Textures のファイル", JoinFileNames(*textures), "A.png:[T],b.png:[T],c.DDS:[T]");
			passed &= ExpectText(detail, "Sub の path", sub->path, root + "/Models/Sub");
			passed &= ExpectText(detail, "Sub の name", sub->name, "Sub");
			if (!textures->files.empty()) {
				passed &= ExpectText(detail, "ファイルの path", textures->files[0].path, root + "/Textures/A.png");
			}
		}

		// FindFolder: 根自身は見つかり、無いパス・ファイル・前方一致だけのパス・空は nullptr
		if (catalog.FindFolder(root) != &tree) {
			detail += "FindFolder(根) ";
			passed = false;
		}
		if (catalog.FindFolder(root + "/Mod") != nullptr || catalog.FindFolder(root + "/Nothing") != nullptr
			|| catalog.FindFolder(root + "/Textures/A.png") != nullptr || catalog.FindFolder(root + "/.hidden_dir") != nullptr
			|| catalog.FindFolder(std::string()) != nullptr) {
			detail += "FindFolder(無いパス) ";
			passed = false;
		}

		// 読み直しても同じ(Refresh は木を作り直す)
		if (!catalog.Refresh() || JoinFolderNames(catalog.GetTree()) != "misc,Models,Textures") {
			detail += "再 Refresh ";
			passed = false;
		}

		// 根が無いときは空の木にして false
		editor::AssetCatalog missing(root + "/NoSuchFolder");
		if (missing.Refresh() || !missing.IsLoaded() || !missing.GetTree().folders.empty() || !missing.GetTree().files.empty()) {
			detail += "根が無いとき ";
			passed = false;
		}

		// 根がファイルのときも false
		editor::AssetCatalog notFolder(root + "/Note.txt");
		if (notFolder.Refresh()) {
			detail += "根がファイルのとき ";
			passed = false;
		}

		Report(results, name, passed, detail);
	}


	/**
	 * T15 アセットの一覧とドラッグ(E9)
	 *
	 * (a) は Editor の AssetCatalog.cpp を直接コンパイルして使う(ImGui に依らないので全構成)。
	 * (b)(E9b)はまだ足していない
	 */
	void RunAssetTests(TestResults& results)
	{
		RunAssetClassifyTest(results);
		RunAssetCatalogTest(results);
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

		RunLeadingCommentExtractTest(results);
#ifdef AQ_DEBUG_IMGUI
		RunStructureEditTests(results);
		RunValueEditTests(results);
		RunLeadingCommentSaveTest(results);
		RunUndoRedoTests(results);
#else
		std::printf("        T10 / T11 / T12 (b) / T13: AQ_DEBUG_IMGUI が無い構成(Release)なので行わない(LevelEditModel が無い)\n");
#endif

		RunSpawnTests(results);
		RunRunModeTests(results);
		RunLogTests(results);
		RunAssetTests(results);

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
