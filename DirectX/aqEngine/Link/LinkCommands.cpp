#include "aq.h"
#include "LinkCommands.h"
#include "Link/LinkService.h"
#include "Level/LevelManager.h"
#include <filesystem>


namespace aq
{
	namespace link
	{
		namespace
		{
			/** hold 系の命令で待てる最大時間(ms) */
			static constexpr int MAX_HOLD_TIMEOUT_MS = 5000;
			/** hold 系の命令で条件を確かめる間隔(ms) */
			static constexpr int HOLD_POLL_INTERVAL_MS = 1;

			/** debug.counter の値(メインスレッドだけが触る) */
			uint64_t sDebugCounter = 0;


			const char* GetGraphicsApiName()
			{
#if defined(ENGINE_GRAPHICS_D3D12)
				return "D3D12";
#elif defined(ENGINE_GRAPHICS_D3D11)
				return "D3D11";
#elif defined(ENGINE_GRAPHICS_VULKAN)
				return "Vulkan";
#elif defined(ENGINE_GRAPHICS_METAL)
				return "Metal";
#else
				return "unknown";
#endif
			}


			/** 実際に動いているモード(edit はまだ未実装で play として動く) */
			const char* GetRunningModeName()
			{
				return "play";
			}


			uint32_t GetCurrentPid()
			{
#if defined(AQ_PLATFORM_WIN32)
				return static_cast<uint32_t>(::GetCurrentProcessId());
#else
				return 0;
#endif
			}


			util::JsonValue MakeNumber(const double value)
			{
				return util::JsonValue(value);
			}


			/**
			 * params の timeoutMs を読む(省略時は上限。上限を超えたら上限に丸める)
			 * @return 値が不正なら false
			 */
			bool ReadHoldTimeoutMs(const util::JsonValue& params, int& timeoutMs)
			{
				timeoutMs = MAX_HOLD_TIMEOUT_MS;
				const util::JsonValue& value = params["timeoutMs"];
				if (value.IsNull()) {
					return true;
				}
				if (!value.IsNumber() || value.AsFloat() < 0.0f) {
					return false;
				}
				if (value.AsFloat() < static_cast<float>(MAX_HOLD_TIMEOUT_MS)) {
					timeoutMs = value.AsInt();
				}
				return true;
			}


			/**
			 * debug.holdStarted を要求の世代宛てに積んでから、条件が成り立つまでメインスレッドを止めて待つ。
			 * 通信スレッドは独立して動くので、待っている間も holdStarted の送信や接続・切断の検出は進む
			 * @return 条件が成り立てば released: true、タイムアウトなら released: false
			 */
			LinkResult Hold(LinkService& service, const LinkRequestContext& context, const int timeoutMs,
			                const std::function<bool()>& isReleased)
			{
				util::JsonValue started = util::JsonValue::MakeObject();
				started.Set("cmd", util::JsonValue(std::string(context.command)));
				service.SendEvent(context.connectionId, "debug.holdStarted", started);

				const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
				bool released = false;
				for (;;) {
					if (isReleased()) {
						released = true;
						break;
					}
					if (std::chrono::steady_clock::now() >= deadline) {
						break;
					}
					std::this_thread::sleep_for(std::chrono::milliseconds(HOLD_POLL_INTERVAL_MS));
				}

				if (!released) {
					service.AddHoldTimeout();
					aq::StartupMarkf("  [link] %s timed out (%d ms)", context.command, timeoutMs);
				}

				util::JsonValue result = util::JsonValue::MakeObject();
				result.Set("released", util::JsonValue(released));
				return LinkResult::Ok(std::move(result));
			}
		}


		void RegisterEngineCommands(LinkService& service)
		{
			LinkService* const link = &service;

			// 疎通と往復遅延の確認
			service.RegisterCommand("ping", [](const util::JsonValue&, const LinkRequestContext&)
				{
					util::JsonValue result = util::JsonValue::MakeObject();
					result.Set("time", MakeNumber(static_cast<double>(aq::Engine::GetTotalTime())));
					return LinkResult::Ok(std::move(result));
				});

			service.RegisterCommand("runtime.info", [link](const util::JsonValue&, const LinkRequestContext&)
				{
					const aq::Engine& engine = aq::Engine::Get();
					util::JsonValue result = util::JsonValue::MakeObject();
					result.Set("protocol",     MakeNumber(static_cast<double>(LINK_PROTOCOL_VERSION)));
					result.Set("app",          util::JsonValue(link->GetAppName()));
					result.Set("mode",         util::JsonValue(std::string(GetRunningModeName())));
					result.Set("graphicsApi",  util::JsonValue(std::string(GetGraphicsApiName())));
					result.Set("renderWidth",  MakeNumber(static_cast<double>(engine.GetRenderWidth())));
					result.Set("renderHeight", MakeNumber(static_cast<double>(engine.GetRenderHeight())));
					result.Set("fps",          MakeNumber(static_cast<double>(aq::Engine::GetFPS())));
					result.Set("pid",          MakeNumber(static_cast<double>(GetCurrentPid())));
					return LinkResult::Ok(std::move(result));
				});

			service.RegisterCommand("runtime.commands", [link](const util::JsonValue&, const LinkRequestContext&)
				{
					util::JsonValue commands = util::JsonValue::MakeArray();
					for (const std::string& name : link->GetCommandNames()) {
						commands.PushBack(util::JsonValue(name));
					}
					util::JsonValue result = util::JsonValue::MakeObject();
					result.Set("commands", std::move(commands));
					return LinkResult::Ok(std::move(result));
				});

			// Level の同期ロード。実体の生成は次の FlushCommands
			service.RegisterCommand("level.load", [](const util::JsonValue& params, const LinkRequestContext&)
				{
					const util::JsonValue& path = params["path"];
					if (!path.IsString() || path.AsString().empty()) {
						return LinkResult::Error("invalid_params", "\"path\" (string) is required");
					}

					// LevelManager::Load はファイルが無いとアサートで止まるので、先に存在を確かめる
					const std::string resolved = aq::res::ResolveExistingResourcePath(path.AsString());
					std::error_code ec;
					if (!std::filesystem::is_regular_file(resolved, ec)) {
						return LinkResult::Error("not_found", "level file not found: " + path.AsString());
					}

					const aq::level::LevelId levelId = aq::level::LevelManager::Get().Load(path.AsString());
					if (!levelId.IsValid()) {
						return LinkResult::Error("failed", "failed to load level: " + path.AsString());
					}

					util::JsonValue result = util::JsonValue::MakeObject();
					result.Set("levelId",         MakeNumber(static_cast<double>(levelId.index)));
					result.Set("levelGeneration", MakeNumber(static_cast<double>(levelId.generation)));
					return LinkResult::Ok(std::move(result));
				});

			service.RegisterCommand("level.reloadAll", [](const util::JsonValue&, const LinkRequestContext&)
				{
					aq::level::LevelManager::Get().ReloadAll();
					return LinkResult::Ok();
				});

			// ---- 評価用の同期点(エディタから呼んではいけない) ----

			// 要求の接続の切断を通信スレッドが検出するまで、メインスレッドを止める
			service.RegisterCommand("debug.holdUntilDisconnected", [link](const util::JsonValue& params, const LinkRequestContext& context)
				{
					int timeoutMs = 0;
					if (!ReadHoldTimeoutMs(params, timeoutMs)) {
						return LinkResult::Error("invalid_params", "\"timeoutMs\" must be a non-negative number");
					}
					const uint64_t generation = context.connectionId;
					return Hold(*link, context, timeoutMs, [link, generation]()
						{
							return link->GetCurrentConnectionId() != generation;
						});
				});

			// 指定の世代の接続を通信スレッドが accept するまで、メインスレッドを止める
			service.RegisterCommand("debug.holdUntilGeneration", [link](const util::JsonValue& params, const LinkRequestContext& context)
				{
					const util::JsonValue& value = params["generation"];
					if (!value.IsNumber() || value.AsFloat() < 1.0f || value.AsFloat() >= 2147483648.0f) {
						return LinkResult::Error("invalid_params", "\"generation\" (positive integer) is required");
					}
					int timeoutMs = 0;
					if (!ReadHoldTimeoutMs(params, timeoutMs)) {
						return LinkResult::Error("invalid_params", "\"timeoutMs\" must be a non-negative number");
					}
					const uint64_t generation = static_cast<uint64_t>(value.AsInt());
					return Hold(*link, context, timeoutMs, [link, generation]()
						{
							return link->GetLastConnectionId() >= generation;
						});
				});

			// 実行されたかどうかを、別の接続から観測するためのカウンタ
			service.RegisterCommand("debug.counter", [](const util::JsonValue& params, const LinkRequestContext&)
				{
					const util::JsonValue& op = params["op"];
					if (!op.IsString() || (op.AsString() != "inc" && op.AsString() != "get")) {
						return LinkResult::Error("invalid_params", "\"op\" must be \"inc\" or \"get\"");
					}
					if (op.AsString() == "inc") {
						++sDebugCounter;
					}
					util::JsonValue result = util::JsonValue::MakeObject();
					result.Set("value", MakeNumber(static_cast<double>(sDebugCounter)));
					return LinkResult::Ok(std::move(result));
				});

			service.RegisterCommand("debug.stats", [link](const util::JsonValue&, const LinkRequestContext&)
				{
					util::JsonValue result = util::JsonValue::MakeObject();
					result.Set("executed",            MakeNumber(static_cast<double>(link->GetExecutedCount())));
					result.Set("discardedBeforeExec", MakeNumber(static_cast<double>(link->GetDiscardedBeforeExecCount())));
					result.Set("droppedStaleOutbox",  MakeNumber(static_cast<double>(link->GetDroppedStaleOutboxCount())));
					result.Set("holdTimeouts",        MakeNumber(static_cast<double>(link->GetHoldTimeoutCount())));
					return LinkResult::Ok(std::move(result));
				});
		}
	}
}
