#include "stdafx.h"
#include "PlayStopSelfTest.h"
#include "PlayModeController.h"
#include "Core/AppHost.h"
#include "ECS/ComponentRegistry.h"
#include "Level/LevelEditModel.h"
#include "Level/LevelManager.h"
#include "Sound/SoundEngine.h"
#include "Util/SimpleJson.h"
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>


namespace editor
{
	namespace
	{
		/** 準備で読み込む Level */
		static constexpr const char* PLAYGROUND_LEVEL_PATH = "Assets/Levels/Playground.level.json";

		/** 1 回の再生で待つフレーム数 */
		static constexpr uint32_t PLAY_FRAME_COUNT = 120;

		/** 生成がそろってから指紋を取るまでのフレーム数(生成の Flush の後の階層変換と描画データの更新を待つ) */
		static constexpr int SETTLE_FRAME_COUNT = 2;

		/** 生成がそろうのを待つ上限のフレーム数(超えたら不一致) */
		static constexpr uint32_t SPAWN_TIMEOUT_FRAME_COUNT = 300;

		/** 起動引数で始めて失敗したときの終了コード */
		static constexpr int FAIL_EXIT_CODE = 3;


		/** 書式付きで StartupLog へ 1 行出す */
		void LogF(const char* format, ...)
		{
			char buffer[512];
			va_list args;
			va_start(args, format);
			std::vsnprintf(buffer, sizeof(buffer), format, args);
			va_end(args);
			aq::StartupLog(buffer);
		}


		/** 文字列を並べ替えて区切り文字で連結する */
		std::string JoinSorted(std::vector<std::string>& items, const char* separator)
		{
			std::sort(items.begin(), items.end());
			std::string out;
			for (size_t i = 0; i < items.size(); ++i) {
				if (i > 0) out += separator;
				out += items[i];
			}
			return out;
		}


		/**
		 * エンティティとその子孫の正規化文字列。
		 * 全コンポーネントを serialize した JSON + 子の文字列を並べ替えて連結したもの。
		 * 名前は空・重複・切り詰めがあるので識別に使わない
		 */
		std::string BuildEntityString(aq::ecs::EntityContext& context, const aq::ecs::EntityHandle handle)
		{
			aq::util::JsonValue components = aq::util::JsonValue::MakeObject();
			for (const auto& [typeInfo, meta] : aq::ecs::ComponentRegistry::Get().GetAll()) {
				if (!meta.has || !meta.serialize) continue;
				if (!meta.has(handle)) continue;

				aq::util::JsonValue value;
				meta.serialize(handle, value);
				const std::string key = (meta.typeName != nullptr) ? std::string(meta.typeName) : std::to_string(typeInfo.GetHash());
				components.Set(key, std::move(value));
			}

			std::vector<std::string> children;
			for (const aq::ecs::EntityHandle& child : context.GetChildren(handle)) {
				children.push_back(BuildEntityString(context, child));
			}

			return "{\"components\":" + aq::util::JsonSerializer::Stringify(components)
			     + ",\"children\":[" + JoinSorted(children, ",") + "]}";
		}


		/** 文字列の項目の長さとハッシュをログへ出す */
		void LogStringItem(const char* name, const std::string& expected, const std::string& actual)
		{
			const std::hash<std::string> hasher;
			LogF("[selftest]   %s: expected len=%zu hash=%016llx / actual len=%zu hash=%016llx",
			     name,
			     expected.size(), static_cast<unsigned long long>(hasher(expected)),
			     actual.size(),   static_cast<unsigned long long>(hasher(actual)));
		}
	}


	void PlayStopSelfTest::Start(const bool exitOnFinish)
	{
		if (IsRunning()) return;

		phase_        = Phase::Starting;
		cycle_        = 0;
		frameCount_   = 0;
		settleFrames_ = -1;
		passed_       = true;
		exitOnFinish_ = exitOnFinish;
		aq::StartupLog("[selftest] start");
	}


	void PlayStopSelfTest::Tick(PlayModeController& controller)
	{
		const aq::RunMode mode = aq::AppHost::Get().GetRunMode();

		switch (phase_) {
		case Phase::Idle:
			break;

		case Phase::Starting:
			if (mode != aq::RunMode::Edit) {
				Abort("not in edit mode");
				break;
			}
			if (!PrepareWorld()) {
				break;
			}
			phase_        = Phase::WaitBaseline;
			frameCount_   = 0;
			settleFrames_ = -1;
			break;

		case Phase::WaitBaseline:
			++frameCount_;
			if (AdvanceSettle(IsEditWorldSpawned())) {
				baseline_ = TakeFingerprint();
				LogF("[selftest] baseline: editModel len=%zu entities len=%zu levels len=%zu ui=%d voices=%u",
				     baseline_.editModel.size(), baseline_.entities.size(), baseline_.levels.size(),
				     baseline_.uiStackSize, baseline_.activeVoices);
				phase_ = Phase::StartCycle;
			} else if (frameCount_ > SPAWN_TIMEOUT_FRAME_COUNT) {
				Abort("baseline world was not spawned");
			}
			break;

		case Phase::StartCycle:
			controller.RequestPlay();
			phase_ = Phase::WaitPlay;
			break;

		case Phase::WaitPlay:
			// 要求は同じフレームの ApplyPendingRequest で実行されるので、次の Tick では Play になっている
			if (mode != aq::RunMode::Play) {
				Abort("play was not applied");
				break;
			}
			phase_      = Phase::Playing;
			frameCount_ = 0;
			[[fallthrough]];

		case Phase::Playing:
			++frameCount_;
			if (frameCount_ >= PLAY_FRAME_COUNT) {
				controller.RequestStop();
				phase_ = Phase::WaitStop;
			}
			break;

		case Phase::WaitStop:
			if (mode != aq::RunMode::Edit) {
				Abort("stop was not applied");
				break;
			}
			// 停止の Flush で読み直した Level はもう生成されていることがあるので、同じ Tick で数え始める(基準と同じ数え方)
			phase_        = Phase::WaitRestore;
			frameCount_   = 0;
			settleFrames_ = -1;
			[[fallthrough]];

		case Phase::WaitRestore:
			++frameCount_;
			if (AdvanceSettle(controller.IsWorldSpawned())) {
				const Fingerprint actual = TakeFingerprint();
				const std::string diffItems = Compare(baseline_, actual);
				FinishCycle(diffItems.empty(), diffItems);
			} else if (frameCount_ > SPAWN_TIMEOUT_FRAME_COUNT) {
				FinishCycle(false, "restore-timeout");
			}
			break;
		}
	}


	bool PlayStopSelfTest::PrepareWorld()
	{
		aq::level::LevelEditModel* model = PlayModeController::FindLevelEditModel();
		if (model == nullptr) {
			Abort("level editor panel is not available");
			return false;
		}

		// 既に読み込んであれば重ねて読まない(Edit のワールドに同じ Level が 2 つ並ぶのを避ける)
		aq::level::LevelManager& levelManager = aq::level::LevelManager::Get();
		if (!levelManager.Find(PLAYGROUND_LEVEL_PATH).IsValid()) {
			if (!levelManager.Load(PLAYGROUND_LEVEL_PATH).IsValid()) {
				Abort("failed to load Playground level");
				return false;
			}
		}

		model->RequestPreview();
		model->ProcessPreviewRequest();
		if (!model->GetPreviewLevelId().IsValid()) {
			Abort("failed to create level editor preview");
			return false;
		}
		return true;
	}


	bool PlayStopSelfTest::AdvanceSettle(const bool spawned)
	{
		if (!spawned) {
			settleFrames_ = -1;
			return false;
		}
		++settleFrames_;
		return settleFrames_ >= SETTLE_FRAME_COUNT;
	}


	void PlayStopSelfTest::FinishCycle(const bool matched, const std::string& diffItems)
	{
		if (matched) {
			LogF("[selftest] cycle %d: MATCH", cycle_ + 1);
		} else {
			LogF("[selftest] cycle %d: MISMATCH %s", cycle_ + 1, diffItems.c_str());
			passed_ = false;
		}

		++cycle_;
		if (cycle_ >= CYCLE_COUNT) {
			Finish();
		} else {
			phase_ = Phase::StartCycle;
		}
	}


	void PlayStopSelfTest::Finish()
	{
		aq::StartupLog(passed_ ? "[selftest] PASS" : "[selftest] FAIL");
		phase_ = Phase::Idle;
		if (exitOnFinish_) {
			aq::Engine::Get().RequestExit(passed_ ? 0 : FAIL_EXIT_CODE);
		}
	}


	void PlayStopSelfTest::Abort(const char* reason)
	{
		LogF("[selftest] abort: %s", reason);
		passed_ = false;
		Finish();
	}


	bool PlayStopSelfTest::IsEditWorldSpawned()
	{
		const aq::level::LevelManager& levelManager = aq::level::LevelManager::Get();

		std::vector<aq::level::LevelId> roots;
		levelManager.CollectLoadedRoots(roots);
		for (const aq::level::LevelId& id : roots) {
			if (!levelManager.IsSpawned(id)) return false;
		}

		const aq::level::LevelEditModel* model = PlayModeController::FindLevelEditModel();
		if (model != nullptr && model->GetPreviewLevelId().IsValid()) {
			if (!levelManager.IsSpawned(model->GetPreviewLevelId())) return false;
		}
		return true;
	}


	PlayStopSelfTest::Fingerprint PlayStopSelfTest::TakeFingerprint()
	{
		Fingerprint fingerprint;
		const aq::level::LevelEditModel* model = PlayModeController::FindLevelEditModel();

		// 編集モデル
		if (model != nullptr) {
			fingerprint.editModel = aq::util::JsonSerializer::Stringify(model->ToJson());
		}

		// エンティティの木。親の無いエンティティをルートにする
		{
			aq::ecs::EntityContext& context = aq::ecs::EntityContext::Get();
			std::vector<aq::ecs::EntityHandle> handles;
			context.CollectAllHandles(handles);

			std::vector<std::string> roots;
			for (const aq::ecs::EntityHandle& handle : handles) {
				if (!context.IsValid(handle)) continue;
				if (context.IsValid(context.GetParent(handle))) continue;
				roots.push_back(BuildEntityString(context, handle));
			}
			fingerprint.entities = JoinSorted(roots, ",");
		}

		// Level。プレビューの登録キーは置き換えのたびに変わるので、キーは比べず有無だけを見る
		{
			const aq::level::LevelManager& levelManager = aq::level::LevelManager::Get();
			const aq::level::LevelId previewId = (model != nullptr) ? model->GetPreviewLevelId() : aq::level::LevelId();

			std::vector<aq::level::LevelId> roots;
			levelManager.CollectLoadedRoots(roots);
			std::vector<std::string> keys;
			for (const aq::level::LevelId& id : roots) {
				if (id == previewId) continue;
				keys.push_back(levelManager.GetSourceKey(id));
			}
			const bool hasPreview = previewId.IsValid() && levelManager.IsLoaded(previewId);
			fingerprint.levels = JoinSorted(keys, "\n") + "\npreview=" + (hasPreview ? "1" : "0");
		}

		fingerprint.uiStackSize  = aq::ui::UIContext::Get().Screens().StackSize();
		fingerprint.activeVoices = aq::sound::SoundEngine::IsAvailable() ? aq::sound::SoundEngine::Get().GetActiveVoiceCount() : 0;
		return fingerprint;
	}


	std::string PlayStopSelfTest::Compare(const Fingerprint& expected, const Fingerprint& actual)
	{
		std::string diffItems;
		const auto addItem = [&diffItems](const char* name)
			{
				if (!diffItems.empty()) diffItems += ' ';
				diffItems += name;
			};

		if (expected.editModel != actual.editModel) {
			addItem("editModel");
			LogStringItem("editModel", expected.editModel, actual.editModel);
		}
		if (expected.entities != actual.entities) {
			addItem("entities");
			LogStringItem("entities", expected.entities, actual.entities);
		}
		if (expected.levels != actual.levels) {
			addItem("levels");
			LogStringItem("levels", expected.levels, actual.levels);
		}
		if (expected.uiStackSize != actual.uiStackSize) {
			addItem("ui");
			LogF("[selftest]   ui: expected=%d actual=%d", expected.uiStackSize, actual.uiStackSize);
		}
		if (expected.activeVoices != actual.activeVoices) {
			addItem("sound");
			LogF("[selftest]   sound: expected=%u actual=%u", expected.activeVoices, actual.activeVoices);
		}
		return diffItems;
	}
}
