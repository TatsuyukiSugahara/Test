#include "aq.h"
#include "SimpleJson.h"
#include <fstream>
#include <cctype>
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
				for (const auto& [key, val] : overrides.GetObject())
				{
					auto it = objVal_.find(key);
					if (it != objVal_.end() && it->second.IsObject() && val.IsObject())
						it->second.Merge(val);
					else
						objVal_[key] = val;
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
			std::ifstream ifs(path, std::ios::binary | std::ios::ate);
			if (!ifs.is_open()) return JsonValue();
			const auto size = static_cast<std::streamsize>(ifs.tellg());
			ifs.seekg(0);
			std::string buf(static_cast<size_t>(size), '\0');
			ifs.read(buf.data(), size);
			return ParseString(buf);
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
					// 整数相当なら小数点なしで出力
					const double d = static_cast<double>(v.AsFloat());
					const long long i = static_cast<long long>(d);
					if (static_cast<double>(i) == d)
					{
						out += std::to_string(i);
					}
					else
					{
						char buf[32];
						std::snprintf(buf, sizeof(buf), "%g", d);
						out += buf;
					}
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
			std::ofstream ofs(path, std::ios::out | std::ios::trunc);
			if (!ofs.is_open()) return false;
			ofs << Stringify(v);
			return ofs.good();
		}

	} // namespace util
} // namespace aq
