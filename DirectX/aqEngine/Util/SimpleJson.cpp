#include "aq.h"
#include "SimpleJson.h"
#include "Resource/Resource.h"
#include <fstream>
#include <cctype>
#include <cfloat>
#include <charconv>
#include <cmath>
#include <cstdlib>

namespace aq
{
	namespace util
	{
		// ---- JsonValue::Merge ---------------------------------------------------

		void JsonValue::Merge(const JsonValue& overrides)
		{
			if (IsObject() && overrides.IsObject())
			{
				Object& dst = EnsureObject();
				for (const auto& [key, val] : overrides.GetObject())
				{
					auto it = dst.find(key);
					if (it != dst.end() && it->second.IsObject() && val.IsObject())
						it->second.Merge(val);
					else
						dst[key] = val;
				}
			}
			else
			{
				*this = overrides;
			}
		}


		// ---- 内部パーサー -------------------------------------------------------

		namespace
		{
			struct ParseCtx
			{
				const char* p;
				const char* end;
			};

			static void SkipWs(ParseCtx& c)
			{
				while (c.p < c.end)
				{
					if (std::isspace(static_cast<unsigned char>(*c.p)))
					{
						++c.p;
					}
					else if (c.p + 1 < c.end && c.p[0] == '/' && c.p[1] == '/')
					{
						c.p += 2;
						while (c.p < c.end && *c.p != '\n') ++c.p;
					}
					else
					{
						break;
					}
				}
			}

			static JsonValue ParseVal(ParseCtx& c);

			// 引用符で囲まれた文字列を読む。c.p は '"' を指している。
			static std::string ParseQuotedStr(ParseCtx& c)
			{
				++c.p; // '"'
				std::string result;
				while (c.p < c.end && *c.p != '"')
				{
					if (*c.p == '\\')
					{
						++c.p;
						if (c.p >= c.end) break;
						switch (*c.p)
						{
							case '"':  result += '"';  break;
							case '\\': result += '\\'; break;
							case '/':  result += '/';  break;
							case 'n':  result += '\n'; break;
							case 'r':  result += '\r'; break;
							case 't':  result += '\t'; break;
							default:   result += *c.p; break;
						}
					}
					else
					{
						result += *c.p;
					}
					++c.p;
				}
				if (c.p < c.end) ++c.p; // 閉じ '"'
				return result;
			}

			static JsonValue ParseNum(ParseCtx& c)
			{
				const char* start = c.p;
				if (c.p < c.end && (*c.p == '-' || *c.p == '+')) ++c.p;
				while (c.p < c.end && std::isdigit(static_cast<unsigned char>(*c.p))) ++c.p;
				if (c.p < c.end && *c.p == '.')
				{
					++c.p;
					while (c.p < c.end && std::isdigit(static_cast<unsigned char>(*c.p))) ++c.p;
				}
				if (c.p < c.end && (*c.p == 'e' || *c.p == 'E'))
				{
					++c.p;
					if (c.p < c.end && (*c.p == '+' || *c.p == '-')) ++c.p;
					while (c.p < c.end && std::isdigit(static_cast<unsigned char>(*c.p))) ++c.p;
				}
				char buf[64] = {};
				size_t len   = static_cast<size_t>(c.p - start);
				if (len >= sizeof(buf)) len = sizeof(buf) - 1;
				std::memcpy(buf, start, len);
				return JsonValue(std::strtod(buf, nullptr));
			}

			static JsonValue ParseArr(ParseCtx& c)
			{
				++c.p; // '['
				JsonValue arr = JsonValue::MakeArray();
				SkipWs(c);
				if (c.p < c.end && *c.p == ']') { ++c.p; return arr; }
				while (c.p < c.end)
				{
					SkipWs(c);
					arr.PushBack(ParseVal(c));
					SkipWs(c);
					if (c.p < c.end && *c.p == ',') { ++c.p; continue; }
					break;
				}
				if (c.p < c.end && *c.p == ']') ++c.p;
				return arr;
			}

			static JsonValue ParseObj(ParseCtx& c)
			{
				++c.p; // '{'
				JsonValue obj = JsonValue::MakeObject();
				SkipWs(c);
				if (c.p < c.end && *c.p == '}') { ++c.p; return obj; }
				while (c.p < c.end)
				{
					SkipWs(c);
					if (c.p >= c.end || *c.p != '"') break; // malformed
					std::string key = ParseQuotedStr(c);
					SkipWs(c);
					if (c.p < c.end && *c.p == ':') ++c.p;
					SkipWs(c);
					JsonValue val = ParseVal(c);
					obj.Set(std::move(key), std::move(val));
					SkipWs(c);
					if (c.p < c.end && *c.p == ',') { ++c.p; continue; }
					break;
				}
				if (c.p < c.end && *c.p == '}') ++c.p;
				return obj;
			}

			static JsonValue ParseVal(ParseCtx& c)
			{
				SkipWs(c);
				if (c.p >= c.end) return JsonValue();
				const char ch = *c.p;
				if (ch == '"')
					return JsonValue(ParseQuotedStr(c));
				if (ch == '{')
					return ParseObj(c);
				if (ch == '[')
					return ParseArr(c);
				if (ch == 't' && c.p + 3 < c.end) { c.p += 4; return JsonValue(true);  }
				if (ch == 'f' && c.p + 4 < c.end) { c.p += 5; return JsonValue(false); }
				if (ch == 'n' && c.p + 3 < c.end) { c.p += 4; return JsonValue();      }
				if (ch == '-' || std::isdigit(static_cast<unsigned char>(ch)))
					return ParseNum(c);
				return JsonValue();
			}

		} // anonymous namespace


		// ---- JsonParser ---------------------------------------------------------

		JsonValue JsonParser::ParseString(std::string_view text)
		{
			ParseCtx c{ text.data(), text.data() + text.size() };
			return ParseVal(c);
		}

		JsonValue JsonParser::ParseFile(const char* path)
		{
			if (path == nullptr) return JsonValue();

			// "Assets/..." のようなアセット相対パスを、実際に開けるパスへ解決する。
			// カレントディレクトリ依存のままだと、CWD をアプリが決められない
			// プラットフォーム(Android は "/"、UWP はパッケージ配下)で JSON が
			// 1 つも読めなくなる。Prefab / Level / UI / AudioBank / Particle が
			// すべて JSON なので、読めないと画面に何も出ない。
			// 該当ファイルが見つからなければ与えられた値がそのまま返るため、
			// エディタが絶対パスを渡す使い方も従来どおり通る。
			const std::string resolved = aq::res::ResolveExistingResourcePath(path);

			std::ifstream ifs(resolved, std::ios::binary | std::ios::ate);
			if (!ifs.is_open()) return JsonValue();
			const auto size = static_cast<std::streamsize>(ifs.tellg());
			ifs.seekg(0);
			std::string buf(static_cast<size_t>(size), '\0');
			ifs.read(buf.data(), size);
			return ParseString(buf);
		}

		std::string JsonParser::ExtractLeadingComments(std::string_view text)
		{
			// UTF-8 の BOM は読み飛ばす(書き戻すときは付けない)
			if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF
				&& static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF) {
				text.remove_prefix(3);
			}

			// 行ごとに見て、空行か // で始まる行が続く間だけを取る。最初のそれ以外の行(ルートの値)で止める
			std::string out;
			size_t pos = 0;
			while (pos < text.size()) {
				const size_t lineEnd = text.find('\n', pos);
				const size_t next    = (lineEnd == std::string_view::npos) ? text.size() : lineEnd + 1;
				std::string_view line = text.substr(pos, next - pos);

				// 行末の改行を外す(CRLF も LF にそろえる)
				while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
					line.remove_suffix(1);
				}
				const size_t first     = line.find_first_not_of(" \t");
				const bool   isBlank   = (first == std::string_view::npos);
				const bool   isComment = !isBlank && line.substr(first, 2) == "//";
				if (!isBlank && !isComment) break;

				out.append(line.data(), line.size());
				out += '\n';
				pos = next;
			}

			// 先頭コメントの後ろに続く空行は落とす(書き出しで JSON の直前に空行が増えないように)
			while (out.size() >= 2 && out[out.size() - 1] == '\n' && out[out.size() - 2] == '\n') {
				out.pop_back();
			}
			// コメントが 1 行も無ければ空にする(空行だけのときに改行を書き足さない)
			if (out.find("//") == std::string::npos) {
				out.clear();
			}
			return out;
		}

		std::string JsonParser::ReadLeadingComments(const char* path)
		{
			if (path == nullptr) return std::string();

			// ParseFile と同じく、アセット相対パスを開けるパスへ解決する
			const std::string resolved = aq::res::ResolveExistingResourcePath(path);

			std::ifstream ifs(resolved, std::ios::binary | std::ios::ate);
			if (!ifs.is_open()) return std::string();
			const auto size = static_cast<std::streamsize>(ifs.tellg());
			ifs.seekg(0);
			std::string buf(static_cast<size_t>(size), '\0');
			ifs.read(buf.data(), size);
			return ExtractLeadingComments(buf);
		}


		// ---- JsonSerializer -----------------------------------------------------

		namespace
		{
			// 特殊文字をエスケープして "..." 形式の JSON 文字列を返す
			std::string EscapeString(const std::string& s)
			{
				std::string out;
				out.reserve(s.size() + 2);
				out += '"';
				for (char c : s)
				{
					switch (c)
					{
						case '"':  out += "\\\""; break;
						case '\\': out += "\\\\"; break;
						case '\n': out += "\\n";  break;
						case '\r': out += "\\r";  break;
						case '\t': out += "\\t";  break;
						default:   out += c;      break;
					}
				}
				out += '"';
				return out;
			}

			std::string MakeIndent(int depth)
			{
				return std::string(static_cast<size_t>(depth) * 2, ' ');
			}

			/** 2^53。絶対値がこれ未満の整数は double で正確に表せる */
			static constexpr double MAX_EXACT_INTEGER = 9007199254740992.0;

			/**
			 * 数値を書き出す(エディタ全体設計 7.8 節「1. 数値」)。次の順に選ぶ。
			 *   1. 整数で絶対値が 2^53 未満 → 小数点なし
			 *   2. float に狭めても同じ値 → float の最短表現。float のフィールドを double に広げた値が
			 *      0.10000000149011612 のような表記にならないようにするため
			 *   3. それ以外 → double の最短表現。override の生 JSON の数値(テキストから読んだ double)を往復させるため
			 * 読み込みは strtod のまま(from_chars の浮動小数点版は iOS で使えない)。
			 */
			void AppendNumber(const double d, std::string& out)
			{
				if (std::fabs(d) < MAX_EXACT_INTEGER && std::trunc(d) == d) {
					out += std::to_string(static_cast<long long>(d));
					return;
				}

				// 範囲外の double を float に変換するのは未定義なので、FLT_MAX 以下のときだけ狭めて比べる
				const bool fitsFloat   = std::isfinite(d) && std::fabs(d) <= static_cast<double>(FLT_MAX);
				const bool sameAsFloat = fitsFloat && static_cast<double>(static_cast<float>(d)) == d;

				char buf[64];
				const std::to_chars_result result = sameAsFloat
					? std::to_chars(buf, buf + sizeof(buf), static_cast<float>(d))
					: std::to_chars(buf, buf + sizeof(buf), d);
				out.append(buf, result.ptr);
			}

			void StringifyImpl(const JsonValue& v, std::string& out, int depth)
			{
				if (v.IsNull())
				{
					out += "null";
				}
				else if (v.IsBool())
				{
					out += v.AsBool() ? "true" : "false";
				}
				else if (v.IsNumber())
				{
					// float に狭めずに書く(整数・float の最短・double の最短の 3 分岐)
					AppendNumber(v.AsDouble(), out);
				}
				else if (v.IsString())
				{
					out += EscapeString(v.AsString());
				}
				else if (v.IsArray())
				{
					const auto& arr = v.GetArray();
					if (arr.empty()) { out += "[]"; return; }

					// 全要素がスカラーなら1行で出力
					bool allScalar = true;
					for (const auto& e : arr)
						if (e.IsArray() || e.IsObject()) { allScalar = false; break; }

					if (allScalar)
					{
						out += '[';
						for (size_t i = 0; i < arr.size(); ++i)
						{
							if (i > 0) out += ", ";
							StringifyImpl(arr[i], out, depth + 1);
						}
						out += ']';
					}
					else
					{
						out += "[\n";
						for (size_t i = 0; i < arr.size(); ++i)
						{
							out += MakeIndent(depth + 1);
							StringifyImpl(arr[i], out, depth + 1);
							if (i + 1 < arr.size()) out += ',';
							out += '\n';
						}
						out += MakeIndent(depth);
						out += ']';
					}
				}
				else if (v.IsObject())
				{
					const auto& obj = v.GetObject();
					if (obj.empty()) { out += "{}"; return; }

					// キーを名前順にソートして出力を安定させる
					std::vector<std::string> keys;
					keys.reserve(obj.size());
					for (const auto& [k, _] : obj) keys.push_back(k);
					std::sort(keys.begin(), keys.end());

					out += "{\n";
					for (size_t i = 0; i < keys.size(); ++i)
					{
						out += MakeIndent(depth + 1);
						out += EscapeString(keys[i]);
						out += ": ";
						StringifyImpl(obj.at(keys[i]), out, depth + 1);
						if (i + 1 < keys.size()) out += ',';
						out += '\n';
					}
					out += MakeIndent(depth);
					out += '}';
				}
			}

		} // anonymous namespace


		std::string JsonSerializer::Stringify(const JsonValue& v, int indent)
		{
			std::string out;
			out.reserve(1024);
			StringifyImpl(v, out, indent);
			return out;
		}

		bool JsonSerializer::WriteFile(const char* path, const JsonValue& v)
		{
			return WriteFile(path, v, std::string_view());
		}

		bool JsonSerializer::WriteFile(const char* path, const JsonValue& v, std::string_view leadingComments)
		{
			// バイナリで開き、改行はどのプラットフォームでも LF にする(テキストモードだと Windows では CRLF になり、
			// LF で管理しているアセットを保存するたびにファイル全体の改行が変わる)
			std::ofstream ofs(path, std::ios::out | std::ios::trunc | std::ios::binary);
			if (!ofs.is_open()) return false;
			ofs << leadingComments;
			ofs << Stringify(v);
			return ofs.good();
		}

	} // namespace util
} // namespace aq
