#pragma once
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>


namespace editor
{
	/** ログの 1 行の重要度(エンジンのログには重要度が無いので、文字列から推定する) */
	enum class LogSeverity : uint8_t
	{
		Info,
		Warning,
		Error,
	};




	/**
	 * コンソールの 1 行
	 */
	struct ConsoleLine
	{
		uint64_t    serial   = 0;                   // 受け取った順の通し番号(1 から。Clear でも戻さない)
		LogSeverity severity = LogSeverity::Info;
		std::string text;
	};




	/**
	 * エンジンのログを受け取って溜める列(コンソールの中身)。
	 * aq::debug::SetLogListener に登録し、受け取った文字列を行に分けて、最大 MAX_LINES 行の環状の列に溜める。
	 * ログはどのスレッドからも来るので、列はミューテックスの下で扱う。ImGui に依らない
	 */
	class ConsoleLog
	{
	public:
		/** 溜める行の上限(超えたら古いものから捨てる) */
		static constexpr size_t MAX_LINES = 4000;


	private:
		/** 環状の列。MAX_LINES に達するまでは末尾に足し、達したら head_ の位置(一番古い行)を上書きする */
		std::vector<ConsoleLine> lines_;
		size_t                   head_ = 0;

		/** 次に振る通し番号 */
		uint64_t nextSerial_ = 1;

		/** 列と通し番号を守る */
		mutable std::mutex mutex_;


	public:
		ConsoleLog() = default;
		ConsoleLog(const ConsoleLog&) = delete;
		ConsoleLog& operator=(const ConsoleLog&) = delete;
		~ConsoleLog();


	public:
		/** エンジンのログの受け取りを始める(受け取る ConsoleLog は 1 つだけ。後から Attach したものに替わる) */
		void Attach();

		/** 受け取りをやめる。戻った後は、このインスタンスへログが渡されることは無い */
		void Detach();

		/** ログ 1 回分の文字列を行に分けて溜める(改行で分け、末尾の空行と \r は落とす) */
		void Append(const char* text);

		/**
		 * 通し番号が sinceSerial より新しい行を out の末尾へ写す。
		 * @return 溜めている一番新しい行の通し番号(次の呼び出しの sinceSerial に渡す。1 行も無ければ sinceSerial のまま)
		 */
		uint64_t CopySince(const uint64_t sinceSerial, std::vector<ConsoleLine>& out) const;

		/** 溜めた行を全部捨てる(通し番号は戻さない) */
		void Clear();

		/** 溜めている行数 */
		size_t GetLineCount() const;


	public:
		/** 1 行の重要度を文字列から推定する(小文字にして error / failed / assert / [crash] / エラー / 失敗 は Error、warn / 警告 は Warning) */
		static LogSeverity EstimateSeverity(const std::string& line);

		/** ログ 1 回分の文字列を行に分ける(\r を落とし、末尾の空行を落とす) */
		static void SplitLines(const char* text, std::vector<std::string>& outLines);


	private:
		/** SetLogListener に登録する関数。Attach しているインスタンスへ渡す */
		static void OnLog(const char* text);
	};
}
