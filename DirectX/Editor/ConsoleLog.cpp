#include "aq.h"
#include "ConsoleLog.h"
#include <algorithm>


namespace editor
{
	namespace
	{
		/** Error と見なす語(小文字にした行に含まれるか見る) */
		static constexpr const char* ERROR_KEYWORDS[] = { "error", "failed", "assert", "[crash]", "エラー", "失敗" };

		/** Warning と見なす語 */
		static constexpr const char* WARNING_KEYWORDS[] = { "warn", "警告" };


		/**
		 * Attach しているインスタンスと、それを守るミューテックス。
		 * ログを渡す間もこのミューテックスを持つので、Detach から戻った後に外したインスタンスへ渡ることは無い
		 */
		std::mutex& GetAttachMutex()
		{
			static std::mutex mutex;
			return mutex;
		}
		ConsoleLog* g_attachedLog = nullptr;


		/** ASCII の英字だけを小文字にする(UTF-8 の多バイト文字はそのまま) */
		std::string ToLowerAscii(const std::string& text)
		{
			std::string lower = text;
			for (char& c : lower) {
				if (c >= 'A' && c <= 'Z') {
					c = static_cast<char>(c - 'A' + 'a');
				}
			}
			return lower;
		}


		/** 語のどれかを含むか */
		template <size_t N>
		bool ContainsAny(const std::string& text, const char* const (&keywords)[N])
		{
			for (const char* keyword : keywords) {
				if (text.find(keyword) != std::string::npos) return true;
			}
			return false;
		}
	}


	ConsoleLog::~ConsoleLog()
	{
		// 外し忘れても、壊れたインスタンスへログが渡らないようにする
		Detach();
	}


	void ConsoleLog::Attach()
	{
		{
			std::lock_guard<std::mutex> lock(GetAttachMutex());
			g_attachedLog = this;
		}
		aq::debug::SetLogListener(&ConsoleLog::OnLog);
	}


	void ConsoleLog::Detach()
	{
		bool wasAttached = false;
		{
			std::lock_guard<std::mutex> lock(GetAttachMutex());
			if (g_attachedLog == this) {
				g_attachedLog = nullptr;
				wasAttached   = true;
			}
		}
		// 別のインスタンスが Attach しているときは、その登録を残す
		if (wasAttached) {
			aq::debug::SetLogListener(nullptr);
		}
	}


	void ConsoleLog::Append(const char* text)
	{
		// 分割と重要度の推定はロックの外で行う(ロックを持つ時間を短くする)
		std::vector<std::string> texts;
		SplitLines(text, texts);
		if (texts.empty()) return;

		std::vector<LogSeverity> severities;
		severities.reserve(texts.size());
		for (const std::string& line : texts) {
			severities.push_back(EstimateSeverity(line));
		}

		std::lock_guard<std::mutex> lock(mutex_);
		for (size_t i = 0; i < texts.size(); ++i) {
			ConsoleLine line;
			line.serial   = nextSerial_++;
			line.severity = severities[i];
			line.text     = std::move(texts[i]);

			if (lines_.size() < MAX_LINES) {
				lines_.push_back(std::move(line));
			} else {
				// 一番古い行を上書きし、次に古い行を先頭にする
				lines_[head_] = std::move(line);
				head_ = (head_ + 1) % MAX_LINES;
			}
		}
	}


	uint64_t ConsoleLog::CopySince(const uint64_t sinceSerial, std::vector<ConsoleLine>& out) const
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (lines_.empty()) return sinceSerial;

		// 溜めている行の通し番号は連続している(古い方から捨て、Clear は全部捨てる)ので、写し始める位置を計算で出せる
		const uint64_t oldestSerial = nextSerial_ - lines_.size();
		const uint64_t firstSerial  = std::max(sinceSerial + 1, oldestSerial);
		if (firstSerial >= nextSerial_) return nextSerial_ - 1;

		const size_t skip  = static_cast<size_t>(firstSerial - oldestSerial);
		const size_t count = lines_.size() - skip;
		out.reserve(out.size() + count);
		for (size_t i = 0; i < count; ++i) {
			out.push_back(lines_[(head_ + skip + i) % lines_.size()]);
		}
		return nextSerial_ - 1;
	}


	void ConsoleLog::Clear()
	{
		std::lock_guard<std::mutex> lock(mutex_);
		lines_.clear();
		head_ = 0;
	}


	size_t ConsoleLog::GetLineCount() const
	{
		std::lock_guard<std::mutex> lock(mutex_);
		return lines_.size();
	}


	LogSeverity ConsoleLog::EstimateSeverity(const std::string& line)
	{
		const std::string lower = ToLowerAscii(line);
		if (ContainsAny(lower, ERROR_KEYWORDS)) return LogSeverity::Error;
		if (ContainsAny(lower, WARNING_KEYWORDS)) return LogSeverity::Warning;
		return LogSeverity::Info;
	}


	void ConsoleLog::SplitLines(const char* text, std::vector<std::string>& outLines)
	{
		outLines.clear();
		if (text == nullptr) return;

		std::string current;
		for (const char* p = text; *p != '\0'; ++p) {
			if (*p == '\r') continue;
			if (*p == '\n') {
				outLines.push_back(std::move(current));
				current.clear();
				continue;
			}
			current += *p;
		}
		outLines.push_back(std::move(current));

		// 末尾の改行で生まれる空行は落とす(途中の空行は残す)
		while (!outLines.empty() && outLines.back().empty()) {
			outLines.pop_back();
		}
	}


	void ConsoleLog::OnLog(const char* text)
	{
		std::lock_guard<std::mutex> lock(GetAttachMutex());
		if (g_attachedLog != nullptr) {
			g_attachedLog->Append(text);
		}
	}
}
