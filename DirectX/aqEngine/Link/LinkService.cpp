#include "aq.h"
#include "LinkService.h"
#include <cmath>
#include <cstdio>


namespace aq
{
	namespace link
	{
		namespace
		{
			/** 文字列を JSON の文字列にする(制御文字はすべてエスケープするので改行は残らない) */
			void WriteCompactString(const std::string& text, std::string& out)
			{
				out += '"';
				for (const char c : text) {
					switch (c) {
					case '"':  out += "\\\""; break;
					case '\\': out += "\\\\"; break;
					case '\n': out += "\\n";  break;
					case '\r': out += "\\r";  break;
					case '\t': out += "\\t";  break;
					default:
						if (static_cast<unsigned char>(c) < 0x20) {
							char buf[8];
							std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned int>(static_cast<unsigned char>(c)));
							out += buf;
						} else {
							out += c;
						}
						break;
					}
				}
				out += '"';
			}


			/** 数値を JSON の数値にする */
			void WriteCompactNumber(const util::JsonValue& value, std::string& out)
			{
				// JsonValue は内部に double を持つが、取り出せるのは AsFloat / AsInt だけ。
				// int に収まる整数は AsInt で正確に出し、それ以外は float の精度で出す
				// (int の範囲外で AsInt を呼ぶと未定義動作になるので、先に float で範囲を確かめる)。
				const float f = value.AsFloat();
				if (!std::isfinite(f)) {
					out += "null";
					return;
				}
				if (f > -2147483648.0f && f < 2147483648.0f) {
					const int i = value.AsInt();
					if (static_cast<float>(i) == f) {
						out += std::to_string(i);
						return;
					}
				}
				char buf[32];
				std::snprintf(buf, sizeof(buf), "%.9g", static_cast<double>(f));
				out += buf;
			}


			/**
			 * JsonValue を改行を含まない 1 行の JSON にする。
			 * JsonSerializer::Stringify はオブジェクトを常に改行付きで整形するので、改行区切りの
			 * フレーミングには使えない。
			 */
			void WriteCompactJson(const util::JsonValue& value, std::string& out)
			{
				if (value.IsBool()) {
					out += value.AsBool() ? "true" : "false";
				} else if (value.IsNumber()) {
					WriteCompactNumber(value, out);
				} else if (value.IsString()) {
					WriteCompactString(value.AsString(), out);
				} else if (value.IsArray()) {
					out += '[';
					const util::JsonValue::Array& array = value.GetArray();
					for (size_t i = 0; i < array.size(); ++i) {
						if (i > 0) {
							out += ',';
						}
						WriteCompactJson(array[i], out);
					}
					out += ']';
				} else if (value.IsObject()) {
					// キーを名前順に並べて出力を安定させる(ログやテストで比べやすくするため)
					const util::JsonValue::Object& object = value.GetObject();
					std::vector<const std::string*> keys;
					keys.reserve(object.size());
					for (const auto& member : object) {
						keys.push_back(&member.first);
					}
					std::sort(keys.begin(), keys.end(), [](const std::string* a, const std::string* b)
						{
							return *a < *b;
						});

					out += '{';
					for (size_t i = 0; i < keys.size(); ++i) {
						if (i > 0) {
							out += ',';
						}
						WriteCompactString(*keys[i], out);
						out += ':';
						WriteCompactJson(object.at(*keys[i]), out);
					}
					out += '}';
				} else {
					out += "null";
				}
			}


			/** 1 行の JSON 文字列を返す */
			std::string ToCompactJson(const util::JsonValue& value)
			{
				std::string out;
				out.reserve(256);
				WriteCompactJson(value, out);
				return out;
			}
		}


		LinkResult LinkResult::Ok(util::JsonValue value)
		{
			LinkResult result;
			result.ok     = true;
			result.result = std::move(value);
			return result;
		}


		LinkResult LinkResult::Error(const char* code, std::string message)
		{
			LinkResult result;
			result.ok           = false;
			result.errorCode    = code ? code : "failed";
			result.errorMessage = std::move(message);
			return result;
		}


		/************************************/




		LinkService* LinkService::sInstance_ = nullptr;


		LinkService::LinkService()
			: executedCount_(0)
			, discardedBeforeExecCount_(0)
			, holdTimeoutCount_(0)
		{
		}


		LinkService::~LinkService()
		{
			socket_.Stop();
		}


		void LinkService::RegisterCommand(const std::string& name, CommandHandler handler)
		{
			if (name.empty() || !handler) {
				aq::StartupMarkf("  [link] ignored invalid command registration");
				return;
			}
			commands_[name] = std::move(handler);
		}


		void LinkService::UnregisterCommand(const std::string& name)
		{
			commands_.erase(name);
		}


		std::vector<std::string> LinkService::GetCommandNames() const
		{
			std::vector<std::string> names;
			names.reserve(commands_.size());
			for (const auto& command : commands_) {
				names.push_back(command.first);
			}
			std::sort(names.begin(), names.end());
			return names;
		}


		void LinkService::Tick()
		{
			const auto startTime = std::chrono::steady_clock::now();
			uint32_t lineCount      = 0;
			uint64_t discardedCount = 0;

			LinkInboxItem item;
			while (lineCount < TICK_MAX_LINE_COUNT) {
				// 時間の確認は 1 命令ごと(命令の途中では止めない)
				if (lineCount > 0) {
					const double elapsedMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - startTime).count();
					if (elapsedMs >= TICK_BUDGET_MS) {
						break;
					}
				}
				if (!socket_.PopInbox(item)) {
					break;
				}

				switch (item.kind) {
				case LinkInboxKind::Connected:
					{
						// hello は、それを生んだ Connected の世代宛てに積む(処理する前に再接続されていたら捨てられる)
						util::JsonValue data = util::JsonValue::MakeObject();
						data.Set("protocol", util::JsonValue(static_cast<double>(LINK_PROTOCOL_VERSION)));
						data.Set("app", util::JsonValue(appName_));
#if defined(AQ_PLATFORM_WIN32)
						data.Set("pid", util::JsonValue(static_cast<double>(::GetCurrentProcessId())));
#else
						data.Set("pid", util::JsonValue(0.0));
#endif
						data.Set("connectionId", util::JsonValue(static_cast<double>(item.connectionId)));
						SendEvent(item.connectionId, "hello", data);
						aq::StartupMarkf("  [link] client connected (connection %llu)", static_cast<unsigned long long>(item.connectionId));
					}
					break;

				case LinkInboxKind::Disconnected:
					aq::StartupMarkf("  [link] client disconnected (connection %llu)", static_cast<unsigned long long>(item.connectionId));
					break;

				case LinkInboxKind::Line:
					++lineCount;
					// 実行開始前に切断を検出済みの要求は、応答を受け取れない相手の編集を反映しないよう捨てる。
					// 確認の直後に切断された場合は実行する(応答は通信スレッドが捨てる)
					if (item.connectionId != socket_.GetCurrentGeneration()) {
						++discardedBeforeExecCount_;
						++discardedCount;
						break;
					}
					ExecuteLine(item);
					break;
				}
			}

			if (discardedCount > 0) {
				aq::StartupMarkf("  [link] discarded %llu request(s) of a disconnected client before execution",
				                 static_cast<unsigned long long>(discardedCount));
			}
		}


		void LinkService::SendEvent(const uint64_t connectionId, const char* name, const util::JsonValue& data)
		{
			util::JsonValue message = util::JsonValue::MakeObject();
			message.Set("type", util::JsonValue(std::string("event")));
			message.Set("event", util::JsonValue(std::string(name ? name : "")));
			message.Set("data", data);
			socket_.PushOutbox(connectionId, ToCompactJson(message));
		}


		void LinkService::ExecuteLine(const LinkInboxItem& item)
		{
			const util::JsonValue message = util::JsonParser::ParseString(item.text);

			// JSON として読めなければ id も分からないので null で返す(接続は切らない)
			if (!message.IsObject()) {
				SendResponse(item.connectionId, util::JsonValue(), LinkResult::Error("bad_request", "message is not a JSON object"));
				return;
			}

			const util::JsonValue& id = message["id"];

			const util::JsonValue& type = message["type"];
			if (!type.IsString() || type.AsString() != "request") {
				SendResponse(item.connectionId, id, LinkResult::Error("bad_request", "\"type\" must be \"request\""));
				return;
			}

			const util::JsonValue& command = message["cmd"];
			if (!command.IsString() || command.AsString().empty()) {
				SendResponse(item.connectionId, id, LinkResult::Error("bad_request", "\"cmd\" is missing"));
				return;
			}

			const util::JsonValue& params = message["params"];
			if (!params.IsNull() && !params.IsObject()) {
				SendResponse(item.connectionId, id, LinkResult::Error("invalid_params", "\"params\" must be an object"));
				return;
			}

			const auto it = commands_.find(command.AsString());
			if (it == commands_.end()) {
				SendResponse(item.connectionId, id, LinkResult::Error("unknown_command", "unknown command: " + command.AsString()));
				return;
			}

			LinkRequestContext context;
			context.connectionId = item.connectionId;
			context.command      = it->first.c_str();

			++executedCount_;
			// ハンドラが Register / Unregister しても壊れないよう、呼ぶ前に写しを取る
			const CommandHandler handler = it->second;
			const LinkResult result = params.IsObject()
				? handler(params, context)
				: handler(util::JsonValue::MakeObject(), context);
			SendResponse(item.connectionId, id, result);
		}


		void LinkService::SendResponse(const uint64_t connectionId, const util::JsonValue& id, const LinkResult& result)
		{
			util::JsonValue message = util::JsonValue::MakeObject();
			message.Set("type", util::JsonValue(std::string("response")));
			message.Set("id", id);
			message.Set("ok", util::JsonValue(result.ok));
			if (result.ok) {
				// result を null で返したハンドラも、エディタ側が常にオブジェクトとして読めるよう {} にそろえる
				message.Set("result", result.result.IsNull() ? util::JsonValue::MakeObject() : result.result);
			} else {
				util::JsonValue error = util::JsonValue::MakeObject();
				error.Set("code", util::JsonValue(result.errorCode));
				error.Set("message", util::JsonValue(result.errorMessage));
				message.Set("error", std::move(error));
			}
			socket_.PushOutbox(connectionId, ToCompactJson(message));
		}


		bool LinkService::Start(const uint16_t port, const char* appName)
		{
			appName_ = appName ? appName : "";
			return socket_.Start(port);
		}


		bool LinkService::Initialize(const uint16_t port, const char* appName)
		{
			if (sInstance_ != nullptr || port == 0) {
				return sInstance_ != nullptr;
			}

#if defined(AQ_PLATFORM_WIN32)
			sInstance_ = new LinkService();
			if (!sInstance_->Start(port, appName)) {
				aq::StartupMarkf("  [link] LinkService disabled (could not listen on port %u)", static_cast<uint32_t>(port));
				delete sInstance_;
				sInstance_ = nullptr;
				return false;
			}
			return true;
#else
			(void)appName;
			aq::StartupMarkf("  [link] -editor-port=%u ignored: LinkService is unsupported on this platform", static_cast<uint32_t>(port));
			return false;
#endif
		}


		void LinkService::Finalize()
		{
			if (sInstance_ == nullptr) {
				return;
			}
			// デストラクタで通信スレッドを止める(未送信データは破棄)
			delete sInstance_;
			sInstance_ = nullptr;
			aq::StartupMarkf("  [link] LinkService finalized");
		}


		LinkService& LinkService::Get()
		{
			EngineAssertMsg(sInstance_ != nullptr, "LinkService::Get: not initialized (check IsAvailable)");
			return *sInstance_;
		}
	}
}
