#include "stdafx.h"
#include "GhostData.h"


namespace app
{
	namespace stage
	{
		namespace
		{
			// .ghost の識別子。'A' 'Q' 'G' 'H' を 1 バイトずつ詰めた 32bit 値。
			static constexpr uint32_t GHOST_FILE_MAGIC = 0x48475141u;

			// ファイル形式の版。レイアウトやサンプルの意味を変えたら上げる。
			// 版違いのファイルは読まずに捨てる (下の LoadFromFile を参照)。
			static constexpr uint32_t GHOST_FILE_VERSION = 1u;

			// サンプル数の上限 [件]。壊れたファイルを掴んだときに巨大な確保をしない歯止め。
			// 30Hz 記録なので 36000 件 = 20 分ぶん。実プレイのクリアタイムより十分大きい。
			static constexpr uint32_t GHOST_MAX_SAMPLE_COUNT = 36000u;

			// 補間の 0 除算よけ。これ未満の時間差 / 距離差は「同じ点」とみなす。
			static constexpr float GHOST_SPAN_EPSILON = 0.0001f;


			// 32bit 値を 1 つずつ読み書きする。struct をそのまま fwrite / fread すると
			// コンパイラとプラットフォームのパディング・アライメントに依存したバイト列に
			// なり、別環境で読めなくなる。メンバごとに入出力してレイアウトを固定する
			// (バイト順は全対象環境が little-endian なのでそのまま載せる)。
			bool WriteU32(FILE* fp, const uint32_t value)
			{
				return fwrite(&value, sizeof(uint32_t), 1, fp) == 1;
			}


			bool WriteF32(FILE* fp, const float value)
			{
				return fwrite(&value, sizeof(float), 1, fp) == 1;
			}


			bool ReadU32(FILE* fp, uint32_t& out)
			{
				return fread(&out, sizeof(uint32_t), 1, fp) == 1;
			}


			bool ReadF32(FILE* fp, float& out)
			{
				return fread(&out, sizeof(float), 1, fp) == 1;
			}
		}


		/**
		 * ゴースト
		 */
		bool GhostData::Evaluate(const float timeSec, GhostSample& out) const
		{
			if (samples.empty()) { return false; }

			// 範囲外は端をクランプ (スタート前 / ゴースト到達後は端の姿勢で止まる)。
			if (timeSec <= samples.front().timeSec) { out = samples.front(); return true; }
			if (timeSec >= samples.back().timeSec)  { out = samples.back();  return true; }

			// timeSec 昇順なので二分探索で前後 2 サンプルを特定する。
			auto it = std::lower_bound(samples.begin(), samples.end(), timeSec,
				[](const GhostSample& sample, const float value)
				{
					return sample.timeSec < value;
				});
			if (it == samples.begin()) { ++it; }
			const GhostSample& s1 = *it;
			const GhostSample& s0 = *(it - 1);

			const float span = s1.timeSec - s0.timeSec;
			const float f    = span > GHOST_SPAN_EPSILON ? (timeSec - s0.timeSec) / span : 0.0f;

			out.timeSec  = timeSec;
			out.distance = aq::math::Lerp(s0.distance, s1.distance, f);
			out.lateral  = aq::math::Lerp(s0.lateral,  s1.lateral,  f);
			out.height   = aq::math::Lerp(s0.height,   s1.height,   f);
			out.roll     = aq::math::Lerp(s0.roll,     s1.roll,     f);
			return true;
		}


		bool GhostData::TimeAtDistance(const float distance, float& outTimeSec) const
		{
			if (samples.empty()) { return false; }

			// 走行は前進のみなので distance も昇順とみなせる。端のクランプは Evaluate と同じ扱い。
			if (distance <= samples.front().distance) { outTimeSec = samples.front().timeSec; return true; }
			if (distance >= samples.back().distance)  { outTimeSec = samples.back().timeSec;  return true; }

			auto it = std::lower_bound(samples.begin(), samples.end(), distance,
				[](const GhostSample& sample, const float value)
				{
					return sample.distance < value;
				});
			if (it == samples.begin()) { ++it; }
			const GhostSample& s1 = *it;
			const GhostSample& s0 = *(it - 1);

			const float span = s1.distance - s0.distance;
			const float f    = span > GHOST_SPAN_EPSILON ? (distance - s0.distance) / span : 0.0f;

			outTimeSec = aq::math::Lerp(s0.timeSec, s1.timeSec, f);
			return true;
		}


		std::shared_ptr<GhostData> GhostData::LoadFromFile(const char* path, const char* stageId)
		{
			if (path == nullptr) { return nullptr; }

			// ファイルが無いのは初回プレイの正常系なので、ここでアサートは出さない。
			FILE* fp = fopen(path, "rb");
			if (fp == nullptr) { return nullptr; }

			uint32_t magic       = 0;
			uint32_t version     = 0;
			uint32_t stageHash   = 0;
			uint32_t sampleCount = 0;
			float    clearTime   = 0.0f;
			const bool headerOk = ReadU32(fp, magic)
			                   && ReadU32(fp, version)
			                   && ReadU32(fp, stageHash)
			                   && ReadF32(fp, clearTime)
			                   && ReadU32(fp, sampleCount);
			if (!headerOk || magic != GHOST_FILE_MAGIC) {
				fclose(fp);
				return nullptr;
			}

			// バージョンやステージ ID が食い違うゴーストは、エラーにせず黙って無視する。
			// コース定義を変えれば同じ distance が別の場所を指すので、古いゴーストは
			// 「コースの外を走る別物」にしかならない。初回プレイと同じ状態から
			// 録り直させるのが正しい挙動 (設計 05 §P23-2)。
			if (version != GHOST_FILE_VERSION) {
				fclose(fp);
				return nullptr;
			}
			if (stageId != nullptr && stageHash != aqHash32(stageId)) {
				fclose(fp);
				return nullptr;
			}

			if (sampleCount == 0 || sampleCount > GHOST_MAX_SAMPLE_COUNT) {
				fclose(fp);
				return nullptr;
			}

			auto data = std::make_shared<GhostData>();
			data->clearTimeSec = clearTime;
			data->samples.resize(sampleCount);
			for (uint32_t i = 0; i < sampleCount; ++i) {
				GhostSample& sample = data->samples[i];
				const bool sampleOk = ReadF32(fp, sample.timeSec)
				                   && ReadF32(fp, sample.distance)
				                   && ReadF32(fp, sample.lateral)
				                   && ReadF32(fp, sample.height)
				                   && ReadF32(fp, sample.roll);
				if (!sampleOk) {
					// 書き出し途中で落ちたなどで切れているファイル。半端な再生をせず捨てる。
					fclose(fp);
					return nullptr;
				}
			}

			fclose(fp);
			return data;
		}


		bool GhostData::SaveToFile(const char* path, const GhostData& data, const char* stageId)
		{
			if (path == nullptr || data.samples.empty()) { return false; }

			FILE* fp = fopen(path, "wb");
			if (fp == nullptr) { return false; }

			const uint32_t sampleCount = static_cast<uint32_t>(data.samples.size());
			bool ok = WriteU32(fp, GHOST_FILE_MAGIC)
			       && WriteU32(fp, GHOST_FILE_VERSION)
			       && WriteU32(fp, stageId != nullptr ? aqHash32(stageId) : 0u)
			       && WriteF32(fp, data.clearTimeSec)
			       && WriteU32(fp, sampleCount);

			for (const auto& sample : data.samples) {
				if (!ok) { break; }
				ok = WriteF32(fp, sample.timeSec)
				  && WriteF32(fp, sample.distance)
				  && WriteF32(fp, sample.lateral)
				  && WriteF32(fp, sample.height)
				  && WriteF32(fp, sample.roll);
			}

			fclose(fp);
			return ok;
		}
	}
}
