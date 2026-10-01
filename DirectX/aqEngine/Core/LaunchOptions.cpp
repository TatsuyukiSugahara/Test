#include "aq.h"
#include "LaunchOptions.h"


namespace aq
{
	namespace
	{
		static constexpr uint64_t MAX_PORT = 65535;


		/** 空白(スペース・タブ・改行)か */
		bool IsSpace(const char c)
		{
			return c == ' ' || c == '\t' || c == '\r' || c == '\n';
		}


		/**
		 * 符号なし整数を読む。数字以外が混じる・空・上限超えは失敗
		 * @param text 読む文字列(接頭辞は呼び出し側で外しておく)
		 * @param base 10 か 16
		 * @param maxValue 許す最大値
		 * @param out 読めた値
		 * @return 読めたら true
		 */
		bool ParseUnsigned(const std::string& text, const uint32_t base, const uint64_t maxValue, uint64_t& out)
		{
			if (text.empty()) {
				return false;
			}

			uint64_t value = 0;
			for (const char c : text) {
				uint32_t digit = 0;
				if (c >= '0' && c <= '9') {
					digit = static_cast<uint32_t>(c - '0');
				} else if (base == 16 && c >= 'a' && c <= 'f') {
					digit = static_cast<uint32_t>(c - 'a') + 10;
				} else if (base == 16 && c >= 'A' && c <= 'F') {
					digit = static_cast<uint32_t>(c - 'A') + 10;
				} else {
					return false;
				}

				// 掛ける前に溢れを判定する(value * base + digit > maxValue)
				if (value > (maxValue - digit) / base) {
					return false;
				}
				value = value * base + digit;
			}
			out = value;
			return true;
		}


		/** 10 進、または 0x / 0X で始まる 16 進を読む */
		bool ParseDecimalOrHex(const std::string& text, const uint64_t maxValue, uint64_t& out)
		{
			if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
				return ParseUnsigned(text.substr(2), 16, maxValue, out);
			}
			return ParseUnsigned(text, 10, maxValue, out);
		}


		/**
		 * 1 個の引数(`-key=value`)を options へ反映する。誤りはログに出して無視する
		 */
		void ApplyArgument(const std::string& argument, LaunchOptions& options)
		{
			// 引用符だけの空引数などは黙って読み飛ばす
			if (argument.empty()) {
				return;
			}

			// 先頭の "--" / "-" を外す。どちらも無ければ引数として扱わない
			size_t keyBegin = 0;
			if (argument.compare(0, 2, "--") == 0) {
				keyBegin = 2;
			} else if (argument[0] == '-') {
				keyBegin = 1;
			} else {
				aq::StartupMarkf("  [launch] ignored argument (expected -key=value): %s", argument.c_str());
				return;
			}

			const size_t equal = argument.find('=', keyBegin);
			if (equal == std::string::npos) {
				aq::StartupMarkf("  [launch] ignored argument (missing '='): %s", argument.c_str());
				return;
			}

			const std::string key   = argument.substr(keyBegin, equal - keyBegin);
			const std::string value = argument.substr(equal + 1);

			if (key == "app") {
				options.appName = value;
				return;
			}

			if (key == "mode") {
				if (value == "play") {
					options.mode = LaunchMode::Play;
				} else if (value == "edit") {
					options.mode = LaunchMode::Edit;
				} else {
					aq::StartupMarkf("  [launch] ignored -mode (expected play|edit): %s", value.c_str());
				}
				return;
			}

			if (key == "editor-port") {
				uint64_t port = 0;
				if (ParseUnsigned(value, 10, MAX_PORT, port) && port != 0) {
					options.editorPort = static_cast<uint16_t>(port);
				} else {
					aq::StartupMarkf("  [launch] ignored -editor-port (expected 1-65535): %s", value.c_str());
				}
				return;
			}

			if (key == "parent-hwnd") {
				uint64_t handle = 0;
				if (ParseDecimalOrHex(value, static_cast<uint64_t>(UINTPTR_MAX), handle) && handle != 0) {
					options.parentWindow = static_cast<uintptr_t>(handle);
				} else {
					aq::StartupMarkf("  [launch] ignored -parent-hwnd (expected decimal or 0x hex): %s", value.c_str());
				}
				return;
			}

			aq::StartupMarkf("  [launch] ignored unknown key: %s", argument.c_str());
		}


		/**
		 * コマンドラインを引数ごとに分ける。
		 * 空白で区切り、`"` で囲んだ範囲の空白は区切りにしない(引用符そのものは取り除く)。
		 * `-app="My App"` も `"-app=My App"` も同じ 1 個の引数になる。
		 */
		std::vector<std::string> SplitCommandLine(const char* cmdLine)
		{
			std::vector<std::string> arguments;
			if (cmdLine == nullptr) {
				return arguments;
			}

			std::string current;
			bool inToken = false;
			bool inQuote = false;
			for (const char* p = cmdLine; *p != '\0'; ++p) {
				const char c = *p;
				if (c == '"') {
					inQuote = !inQuote;
					inToken = true;
					continue;
				}
				if (!inQuote && IsSpace(c)) {
					if (inToken) {
						arguments.push_back(current);
						current.clear();
						inToken = false;
					}
					continue;
				}
				current.push_back(c);
				inToken = true;
			}
			if (inQuote) {
				aq::StartupMarkf("  [launch] unterminated quote in command line");
			}
			if (inToken) {
				arguments.push_back(current);
			}
			return arguments;
		}
	}


	LaunchOptions LaunchOptions::Parse(const int argc, const char* const* argv)
	{
		LaunchOptions options;
		if (argv == nullptr) {
			return options;
		}

		// argv[0] はプログラム名
		for (int i = 1; i < argc; ++i) {
			if (argv[i] != nullptr) {
				ApplyArgument(argv[i], options);
			}
		}
		return options;
	}


	LaunchOptions LaunchOptions::ParseCommandLine(const char* cmdLine)
	{
		LaunchOptions options;
		for (const std::string& argument : SplitCommandLine(cmdLine)) {
			ApplyArgument(argument, options);
		}
		return options;
	}
}
