// SoftwareMixer の単体テスト相当コマンドラインツール（Mac移植設計 §9 P1）。
//
//   MixerTest --selftest [-o out.wav]
//   MixerTest -o out.wav --in a.wav [--pitch 1.5] [--swap] [--vol 0.5] [--bus SE] [--in b.wav ...]
//
// aq.h（PCH）には依存しない。SoftwareMixer.h / WavDecoder.h はどちらも
// SoundTypes.h しか要求しない可搬ヘッダなので、そのまま include できる。
#include "Sound/Mixer/SoftwareMixer.h"
#include "Sound/Decoder/WavDecoder.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <string>
#include <vector>


namespace
{
	using aq::sound::SoftwareMixer;
	using aq::sound::SoundBusId;
	using aq::sound::SoundFormat;
	using aq::sound::SubmitResult;

	static constexpr uint32_t DEFAULT_SAMPLE_RATE = 48000u;
	static constexpr uint16_t DEFAULT_CHANNELS    = 2u;
	static constexpr uint32_t RENDER_CHUNK_FRAMES = 128u;
	static constexpr uint32_t SUBMIT_CHUNK_FRAMES = 4096u;

	/** セルフテストの信号長・出力長（フレーム） */
	static constexpr uint32_t SELFTEST_SOURCE_FRAMES = 1024u;
	static constexpr uint32_t SELFTEST_OUTPUT_FRAMES = 600u;

	/** セルフテストの許容誤差 */
	static constexpr float SELFTEST_TOLERANCE = 1.0e-5f;


	/**
	 * 1 ボイス分の指定
	 */
	struct VoiceDesc
	{
		std::string path;
		float       pitch  = 1.0f;
		float       volume = 1.0f;
		bool        swap   = false;
		SoundBusId  bus    = SoundBusId::SE;
	};


	/**
	 * 実行時オプション
	 */
	struct Options
	{
		bool                   selfTest    = false;
		bool                   writePcm16  = false;
		std::string            outPath;
		uint32_t               sampleRate  = DEFAULT_SAMPLE_RATE;
		uint16_t               channels    = DEFAULT_CHANNELS;
		float                  masterVolume = 1.0f;
		double                 maxSeconds  = 0.0;   // 0 = 全ボイスが終わるまで
		std::vector<VoiceDesc> voices;
	};


	void PrintUsage()
	{
		std::printf(
			"MixerTest - SoftwareMixer の単体テスト用ツール\n"
			"\n"
			"  MixerTest --selftest [-o out.wav]\n"
			"      2 ボイス(片方ピッチ 1.5 + 出力行列で左右反転)のミックス結果を\n"
			"      解析的な期待波形と突き合わせて PASS/FAIL を出す。\n"
			"\n"
			"  MixerTest -o out.wav --in a.wav [voice opts] [--in b.wav [voice opts]] ...\n"
			"      実 WAV をミックスして WAV に書き出す。\n"
			"\n"
			"共通オプション:\n"
			"  -o, --out <path>     出力 WAV\n"
			"      --rate <hz>      出力サンプルレート (既定 48000)\n"
			"      --channels <n>   出力チャンネル数 (既定 2)\n"
			"      --master <v>     マスタ音量 (既定 1.0)\n"
			"      --seconds <s>    出力の長さ上限 (既定 0 = 全ボイス終了まで)\n"
			"      --pcm16          出力を 16bit PCM にする (既定 32bit float)\n"
			"\n"
			"ボイスオプション (直前の --in に適用):\n"
			"      --pitch <r>      SetFrequencyRatio 相当 (既定 1.0)\n"
			"      --vol <v>        ボイス音量 (既定 1.0)\n"
			"      --swap           出力行列で左右を入れ替える\n"
			"      --bus <name>     Master / BGM / SE / Voice (既定 SE)\n");
	}


	bool ParseBus(const char* name, SoundBusId& out)
	{
		if (std::strcmp(name, "Master") == 0) { out = SoundBusId::Master; return true; }
		if (std::strcmp(name, "BGM") == 0)    { out = SoundBusId::BGM;    return true; }
		if (std::strcmp(name, "SE") == 0)     { out = SoundBusId::SE;     return true; }
		if (std::strcmp(name, "Voice") == 0)  { out = SoundBusId::Voice;  return true; }
		return false;
	}


	bool ParseArgs(int argc, char** argv, Options& options)
	{
		for (int i = 1; i < argc; ++i) {
			const std::string arg = argv[i];
			const bool        hasNext = (i + 1) < argc;

			auto next = [&]() -> const char* { return argv[++i]; };

			if (arg == "--selftest") {
				options.selfTest = true;
			}
			else if ((arg == "-o" || arg == "--out") && hasNext) {
				options.outPath = next();
			}
			else if (arg == "--rate" && hasNext) {
				options.sampleRate = static_cast<uint32_t>(std::atoi(next()));
			}
			else if (arg == "--channels" && hasNext) {
				options.channels = static_cast<uint16_t>(std::atoi(next()));
			}
			else if (arg == "--master" && hasNext) {
				options.masterVolume = static_cast<float>(std::atof(next()));
			}
			else if (arg == "--seconds" && hasNext) {
				options.maxSeconds = std::atof(next());
			}
			else if (arg == "--pcm16") {
				options.writePcm16 = true;
			}
			else if (arg == "--in" && hasNext) {
				VoiceDesc desc;
				desc.path = next();
				options.voices.push_back(desc);
			}
			else if (arg == "--pitch" && hasNext) {
				if (options.voices.empty()) { return false; }
				options.voices.back().pitch = static_cast<float>(std::atof(next()));
			}
			else if (arg == "--vol" && hasNext) {
				if (options.voices.empty()) { return false; }
				options.voices.back().volume = static_cast<float>(std::atof(next()));
			}
			else if (arg == "--swap") {
				if (options.voices.empty()) { return false; }
				options.voices.back().swap = true;
			}
			else if (arg == "--bus" && hasNext) {
				if (options.voices.empty()) { return false; }
				if (!ParseBus(next(), options.voices.back().bus)) { return false; }
			}
			else {
				std::fprintf(stderr, "不明な引数: %s\n", arg.c_str());
				return false;
			}
		}
		return true;
	}


	/**
	 * リトルエンディアン書き出し
	 */
	void WriteU16(std::FILE* fp, const uint16_t v)
	{
		const uint8_t bytes[2] = { static_cast<uint8_t>(v & 0xffu), static_cast<uint8_t>((v >> 8) & 0xffu) };
		std::fwrite(bytes, 1, sizeof(bytes), fp);
	}


	void WriteU32(std::FILE* fp, const uint32_t v)
	{
		const uint8_t bytes[4] = {
			static_cast<uint8_t>(v & 0xffu),
			static_cast<uint8_t>((v >> 8) & 0xffu),
			static_cast<uint8_t>((v >> 16) & 0xffu),
			static_cast<uint8_t>((v >> 24) & 0xffu),
		};
		std::fwrite(bytes, 1, sizeof(bytes), fp);
	}


	/**
	 * インタリーブ float を WAV へ書き出す
	 * @param pcm16 true なら 16bit 整数 PCM、false なら 32bit IEEE float
	 */
	bool WriteWav(const char* path, const std::vector<float>& samples,
	              const uint32_t sampleRate, const uint16_t channels, const bool pcm16)
	{
		std::FILE* fp = std::fopen(path, "wb");
		if (fp == nullptr) {
			std::fprintf(stderr, "出力を開けません: %s\n", path);
			return false;
		}

		const uint16_t bitsPerSample = pcm16 ? 16u : 32u;
		const uint16_t formatTag     = pcm16 ? 1u : 3u;   // 1 = PCM / 3 = IEEE float
		const uint16_t blockAlign    = static_cast<uint16_t>(channels * (bitsPerSample / 8u));
		const uint32_t dataBytes     = static_cast<uint32_t>(samples.size()) * (bitsPerSample / 8u);

		std::fwrite("RIFF", 1, 4, fp);
		WriteU32(fp, 36u + dataBytes);
		std::fwrite("WAVE", 1, 4, fp);

		std::fwrite("fmt ", 1, 4, fp);
		WriteU32(fp, 16u);
		WriteU16(fp, formatTag);
		WriteU16(fp, channels);
		WriteU32(fp, sampleRate);
		WriteU32(fp, sampleRate * blockAlign);
		WriteU16(fp, blockAlign);
		WriteU16(fp, bitsPerSample);

		std::fwrite("data", 1, 4, fp);
		WriteU32(fp, dataBytes);

		if (pcm16) {
			std::vector<int16_t> converted(samples.size());
			for (size_t i = 0; i < samples.size(); ++i) {
				float v = samples[i];
				if (v > 1.0f)  { v = 1.0f; }
				if (v < -1.0f) { v = -1.0f; }
				converted[i] = static_cast<int16_t>(v * 32767.0f);
			}
			std::fwrite(converted.data(), sizeof(int16_t), converted.size(), fp);
		}
		else {
			std::fwrite(samples.data(), sizeof(float), samples.size(), fp);
		}

		std::fclose(fp);
		return true;
	}


	/**
	 * 左右反転（またはモノラルを右だけへ）の出力行列を作る
	 * 並びは XAudio2 の SetOutputMatrix と同じ matrix[dstIndex * srcChannels + srcIndex]。
	 */
	void MakeSwapMatrix(const uint32_t srcChannels, const uint32_t dstChannels, std::vector<float>& out)
	{
		out.assign(static_cast<size_t>(srcChannels) * dstChannels, 0.0f);

		// モノラル入力は「右チャンネルだけへ送る」を左右反転とみなす。
		if (srcChannels == 1u && dstChannels >= 2u) {
			out[1] = 1.0f;
			return;
		}

		for (uint32_t d = 0; d < dstChannels; ++d) {
			uint32_t s = d;
			if (d == 0u)      { s = 1u; }
			else if (d == 1u) { s = 0u; }
			if (s < srcChannels) {
				out[static_cast<size_t>(d) * srcChannels + s] = 1.0f;
			}
		}
	}


	/**
	 * 実 WAV をミックスする
	 */
	int RunMix(const Options& options)
	{
		if (options.outPath.empty() || options.voices.empty()) {
			PrintUsage();
			return 1;
		}

		SoundFormat outputFormat;
		outputFormat.sampleRate    = options.sampleRate;
		outputFormat.channels      = options.channels;
		outputFormat.bitsPerSample = 32u;
		outputFormat.isFloat       = true;

		SoftwareMixer mixer;
		if (!mixer.Initialize(outputFormat)) {
			std::fprintf(stderr, "ミキサの初期化に失敗しました\n");
			return 1;
		}
		mixer.SetMasterVolume(options.masterVolume);

		// ── 入力の読み込みとボイス確保 ──
		struct Source
		{
			SoundFormat            format;
			std::vector<uint8_t>   pcm;
			SoftwareMixer::VoiceId voiceId = SoftwareMixer::INVALID_VOICE_ID;
			size_t                 offset  = 0;
		};
		std::vector<Source> sources(options.voices.size());

		for (size_t i = 0; i < options.voices.size(); ++i) {
			const VoiceDesc& desc   = options.voices[i];
			Source&          source = sources[i];

			if (!aq::sound::WavDecoder::DecodeFile(desc.path.c_str(), source.format, source.pcm)) {
				std::fprintf(stderr, "WAV を読めません: %s\n", desc.path.c_str());
				return 1;
			}

			source.voiceId = mixer.AcquireVoice(source.format, desc.bus);
			if (source.voiceId == SoftwareMixer::INVALID_VOICE_ID) {
				std::fprintf(stderr, "ボイスを確保できません: %s\n", desc.path.c_str());
				return 1;
			}

			mixer.SetVoiceVolume(source.voiceId, desc.volume);
			mixer.SetFrequencyRatio(source.voiceId, desc.pitch);
			if (desc.swap) {
				std::vector<float> matrix;
				MakeSwapMatrix(source.format.channels, outputFormat.channels, matrix);
				mixer.SetOutputMatrix(source.voiceId, source.format.channels, outputFormat.channels, matrix.data());
			}
			mixer.Start(source.voiceId);

			std::printf("[in ] %s  %u Hz / %u ch / %u bit%s  frames=%llu  pitch=%.3f vol=%.3f%s\n",
			            desc.path.c_str(), source.format.sampleRate, source.format.channels,
			            source.format.bitsPerSample, source.format.isFloat ? " float" : "",
			            static_cast<unsigned long long>(source.pcm.size() / (source.format.BytesPerFrame() != 0u ? source.format.BytesPerFrame() : 1u)),
			            desc.pitch, desc.volume, desc.swap ? " swap" : "");
		}

		// ── レンダリング ──
		const uint64_t maxFrames = options.maxSeconds > 0.0
			? static_cast<uint64_t>(options.maxSeconds * outputFormat.sampleRate)
			: 0u;

		std::vector<float> output;
		std::vector<float> chunk(static_cast<size_t>(RENDER_CHUNK_FRAMES) * outputFormat.channels);
		uint64_t           renderedFrames = 0u;

		for (;;) {
			// 供給（背圧が返ったら次のレンダリング後に再投入する）
			bool anyPending = false;
			for (size_t i = 0; i < sources.size(); ++i) {
				Source&        source        = sources[i];
				const uint32_t bytesPerFrame = source.format.BytesPerFrame();
				if (bytesPerFrame == 0u) {
					continue;
				}
				while (source.offset < source.pcm.size()) {
					const size_t remain = source.pcm.size() - source.offset;
					const size_t bytes  = remain < static_cast<size_t>(SUBMIT_CHUNK_FRAMES) * bytesPerFrame
						? remain
						: static_cast<size_t>(SUBMIT_CHUNK_FRAMES) * bytesPerFrame;
					const bool   isLast = (source.offset + bytes) >= source.pcm.size();

					const SubmitResult result = mixer.SubmitBuffer(
						source.voiceId, source.pcm.data() + source.offset,
						static_cast<uint32_t>(bytes), isLast);
					if (result != SubmitResult::Accepted) {
						break;
					}
					source.offset += bytes;
				}
				if (source.offset < source.pcm.size()) {
					anyPending = true;
				}
			}

			mixer.Render(chunk.data(), RENDER_CHUNK_FRAMES);
			mixer.Update();
			output.insert(output.end(), chunk.begin(), chunk.end());
			renderedFrames += RENDER_CHUNK_FRAMES;

			if (maxFrames != 0u && renderedFrames >= maxFrames) {
				break;
			}

			bool allFinished = !anyPending;
			for (const Source& source : sources) {
				if (!mixer.IsFinished(source.voiceId)) {
					allFinished = false;
					break;
				}
			}
			if (allFinished) {
				break;
			}
			if (maxFrames == 0u && renderedFrames > static_cast<uint64_t>(outputFormat.sampleRate) * 60ull * 30ull) {
				std::fprintf(stderr, "30 分を超えたので打ち切ります\n");
				break;
			}
		}

		for (const Source& source : sources) {
			std::printf("[out] voice %u: consumed=%llu frames finished=%s\n",
			            source.voiceId,
			            static_cast<unsigned long long>(mixer.GetConsumedFrames(source.voiceId)),
			            mixer.IsFinished(source.voiceId) ? "yes" : "no");
		}
		std::printf("[out] rendered=%llu frames underrun=%llu\n",
		            static_cast<unsigned long long>(mixer.GetRenderedFrames()),
		            static_cast<unsigned long long>(mixer.GetUnderrunCount()));

		if (!WriteWav(options.outPath.c_str(), output, outputFormat.sampleRate, outputFormat.channels, options.writePcm16)) {
			return 1;
		}
		std::printf("[out] %s\n", options.outPath.c_str());
		return 0;
	}


	/**
	 * セルフテスト
	 *
	 * 線形リサンプルは 1 次補間なので、入力を「一次関数(ランプ)」にすると
	 * 任意のリサンプル比で出力が解析的に決まる。これを使って
	 *   - ボイス 1: ピッチ 1.0・そのまま出力
	 *   - ボイス 2: ピッチ 1.5・出力行列で左右反転
	 *   - ボイス音量 / バス音量 / マスタ音量
	 * をまとめて突き合わせる。
	 */
	int RunSelfTest(const Options& options)
	{
		SoundFormat outputFormat;
		outputFormat.sampleRate    = DEFAULT_SAMPLE_RATE;
		outputFormat.channels      = 2u;
		outputFormat.bitsPerSample = 32u;
		outputFormat.isFloat       = true;

		SoundFormat sourceFormat = outputFormat;   // 同レート・ステレオ・float

		// ── 入力信号（すべて 2 の冪で表せる係数にして浮動小数の誤差を避ける）──
		auto voiceASignalL = [](const double p) { return 0.25 + p * (1.0 / 1024.0); };
		auto voiceASignalR = [](const double p) { return -0.125 + p * (1.0 / 2048.0); };
		auto voiceBSignalL = [](const double p) { return 0.5 - p * (1.0 / 4096.0); };
		auto voiceBSignalR = [](const double p) { return 0.0625 + p * (1.0 / 8192.0); };

		std::vector<float> pcmA(static_cast<size_t>(SELFTEST_SOURCE_FRAMES) * 2u);
		std::vector<float> pcmB(static_cast<size_t>(SELFTEST_SOURCE_FRAMES) * 2u);
		for (uint32_t n = 0; n < SELFTEST_SOURCE_FRAMES; ++n) {
			pcmA[n * 2u + 0u] = static_cast<float>(voiceASignalL(n));
			pcmA[n * 2u + 1u] = static_cast<float>(voiceASignalR(n));
			pcmB[n * 2u + 0u] = static_cast<float>(voiceBSignalL(n));
			pcmB[n * 2u + 1u] = static_cast<float>(voiceBSignalR(n));
		}

		// ── ゲイン設定 ──
		static constexpr float VOICE_A_VOLUME  = 0.5f;
		static constexpr float VOICE_B_VOLUME  = 1.0f;
		static constexpr float BUS_SE_VOLUME   = 1.0f;
		static constexpr float BUS_BGM_VOLUME  = 0.75f;
		static constexpr float MASTER_VOLUME   = 0.8f;
		static constexpr float VOICE_B_PITCH   = 1.5f;

		SoftwareMixer mixer;
		if (!mixer.Initialize(outputFormat)) {
			std::fprintf(stderr, "ミキサの初期化に失敗しました\n");
			return 1;
		}
		mixer.SetMasterVolume(MASTER_VOLUME);
		mixer.SetBusVolume(SoundBusId::SE, BUS_SE_VOLUME);
		mixer.SetBusVolume(SoundBusId::BGM, BUS_BGM_VOLUME);

		const SoftwareMixer::VoiceId voiceA = mixer.AcquireVoice(sourceFormat, SoundBusId::SE);
		const SoftwareMixer::VoiceId voiceB = mixer.AcquireVoice(sourceFormat, SoundBusId::BGM);
		if (voiceA == SoftwareMixer::INVALID_VOICE_ID || voiceB == SoftwareMixer::INVALID_VOICE_ID) {
			std::fprintf(stderr, "ボイスを確保できません\n");
			return 1;
		}

		mixer.SetVoiceVolume(voiceA, VOICE_A_VOLUME);
		mixer.SetVoiceVolume(voiceB, VOICE_B_VOLUME);
		mixer.SetFrequencyRatio(voiceB, VOICE_B_PITCH);

		// 左右反転（dstIndex * srcChannels + srcIndex）
		const float swapMatrix[4] = { 0.0f, 1.0f,
		                              1.0f, 0.0f };
		mixer.SetOutputMatrix(voiceB, 2u, 2u, swapMatrix);

		if (mixer.SubmitBuffer(voiceA, pcmA.data(), static_cast<uint32_t>(pcmA.size() * sizeof(float)), false) != SubmitResult::Accepted
			|| mixer.SubmitBuffer(voiceB, pcmB.data(), static_cast<uint32_t>(pcmB.size() * sizeof(float)), false) != SubmitResult::Accepted) {
			std::fprintf(stderr, "PCM の投入に失敗しました\n");
			return 1;
		}

		mixer.Start(voiceA);
		mixer.Start(voiceB);

		// ── レンダリング ──
		std::vector<float> output;
		std::vector<float> chunk(static_cast<size_t>(RENDER_CHUNK_FRAMES) * outputFormat.channels);
		output.reserve(static_cast<size_t>(SELFTEST_OUTPUT_FRAMES) * outputFormat.channels);
		while (output.size() < static_cast<size_t>(SELFTEST_OUTPUT_FRAMES) * outputFormat.channels) {
			mixer.Render(chunk.data(), RENDER_CHUNK_FRAMES);
			mixer.Update();
			output.insert(output.end(), chunk.begin(), chunk.end());
		}

		// ── 期待波形と突き合わせ ──
		//   out.L[i] = (A.L(i) * volA * busSE + B.R(1.5i) * volB * busBGM) * master
		//   out.R[i] = (A.R(i) * volA * busSE + B.L(1.5i) * volB * busBGM) * master
		float maxErrorL = 0.0f;
		float maxErrorR = 0.0f;
		for (uint32_t i = 0; i < SELFTEST_OUTPUT_FRAMES; ++i) {
			const double posA = static_cast<double>(i);
			const double posB = static_cast<double>(i) * VOICE_B_PITCH;

			const double expectedL = (voiceASignalL(posA) * VOICE_A_VOLUME * BUS_SE_VOLUME
			                        + voiceBSignalR(posB) * VOICE_B_VOLUME * BUS_BGM_VOLUME) * MASTER_VOLUME;
			const double expectedR = (voiceASignalR(posA) * VOICE_A_VOLUME * BUS_SE_VOLUME
			                        + voiceBSignalL(posB) * VOICE_B_VOLUME * BUS_BGM_VOLUME) * MASTER_VOLUME;

			const float errorL = std::fabs(output[i * 2u + 0u] - static_cast<float>(expectedL));
			const float errorR = std::fabs(output[i * 2u + 1u] - static_cast<float>(expectedR));
			if (errorL > maxErrorL) { maxErrorL = errorL; }
			if (errorR > maxErrorR) { maxErrorR = errorR; }

			if (errorL > SELFTEST_TOLERANCE || errorR > SELFTEST_TOLERANCE) {
				std::fprintf(stderr,
					"FAIL frame %u: L got %.8f want %.8f / R got %.8f want %.8f\n",
					i, static_cast<double>(output[i * 2u + 0u]), expectedL,
					static_cast<double>(output[i * 2u + 1u]), expectedR);
				return 1;
			}
		}

		std::printf("[selftest] 波形一致: maxError L=%.3e R=%.3e (許容 %.1e)\n",
		            static_cast<double>(maxErrorL), static_cast<double>(maxErrorR),
		            static_cast<double>(SELFTEST_TOLERANCE));

		// ── 消費フレーム数の検算 ──
		//   線形補間は 1 フレーム先読みするため、消費数は「再生位置 + 1」相当になる。
		const uint32_t renderedFrames = static_cast<uint32_t>(output.size() / outputFormat.channels);
		const uint64_t consumedA      = mixer.GetConsumedFrames(voiceA);
		const uint64_t consumedB      = mixer.GetConsumedFrames(voiceB);
		std::printf("[selftest] rendered=%u consumed: A=%llu B=%llu (期待の目安 A=%u B=%u)\n",
		            renderedFrames,
		            static_cast<unsigned long long>(consumedA),
		            static_cast<unsigned long long>(consumedB),
		            renderedFrames + 2u,
		            static_cast<uint32_t>(renderedFrames * VOICE_B_PITCH) + 2u);
		if (consumedB <= consumedA) {
			std::fprintf(stderr, "FAIL: ピッチ 1.5 のボイスの消費フレーム数が増えていません\n");
			return 1;
		}

		// ── endOfStream で自然終了して IsFinished が立つか ──
		{
			// 供給を使い切ったボイスを鳴らしたままだと供給切れが計上されるので先に止める。
			mixer.Stop(voiceA);
			mixer.Stop(voiceB);

			const SoftwareMixer::VoiceId voiceC = mixer.AcquireVoice(sourceFormat, SoundBusId::SE);
			if (voiceC == SoftwareMixer::INVALID_VOICE_ID) {
				std::fprintf(stderr, "FAIL: 3 本目のボイスを確保できません\n");
				return 1;
			}
			std::vector<float> shortPcm(64u * 2u, 0.25f);
			if (mixer.SubmitBuffer(voiceC, shortPcm.data(), static_cast<uint32_t>(shortPcm.size() * sizeof(float)), true)
				!= SubmitResult::Accepted) {
				std::fprintf(stderr, "FAIL: endOfStream 付き投入が拒否されました\n");
				return 1;
			}
			mixer.Start(voiceC);
			for (int pass = 0; pass < 4; ++pass) {
				mixer.Render(chunk.data(), RENDER_CHUNK_FRAMES);
				mixer.Update();
			}
			if (!mixer.IsFinished(voiceC)) {
				std::fprintf(stderr, "FAIL: endOfStream 後に IsFinished が立ちません\n");
				return 1;
			}
			mixer.ReleaseVoice(voiceC);
			mixer.Render(chunk.data(), RENDER_CHUNK_FRAMES);
			mixer.Update();
			std::printf("[selftest] endOfStream / IsFinished / ReleaseVoice OK\n");
		}

		std::printf("[selftest] underrun=%llu retireOverflow=%llu\n",
		            static_cast<unsigned long long>(mixer.GetUnderrunCount()),
		            static_cast<unsigned long long>(mixer.GetRetireOverflowCount()));

		if (!options.outPath.empty()) {
			if (!WriteWav(options.outPath.c_str(), output, outputFormat.sampleRate, outputFormat.channels, options.writePcm16)) {
				return 1;
			}
			std::printf("[selftest] %s に書き出しました\n", options.outPath.c_str());
		}

		std::printf("[selftest] PASS\n");
		return 0;
	}
}


int main(int argc, char** argv)
{
	Options options;
	if (!ParseArgs(argc, argv, options)) {
		PrintUsage();
		return 1;
	}
	if (argc <= 1) {
		PrintUsage();
		return 1;
	}

	if (options.selfTest) {
		return RunSelfTest(options);
	}
	return RunMix(options);
}
