#include "aq.h"
// Android 以外では空 TU。
#if defined(AQ_PLATFORM_ANDROID)
#include "Sound/AAudio/AAudioSoundBackend.h"
#include "Sound/Mixer/MixerSoundVoice.h"

#include <aaudio/AAudio.h>


namespace aq
{
	namespace sound
	{
		namespace
		{
			/** 要求する出力サンプルレート。SoftwareMixer の想定(§8.2)と揃える */
			constexpr uint32_t OUTPUT_SAMPLE_RATE = 48000u;

			/** 要求する出力チャンネル数(ステレオ固定) */
			constexpr uint16_t OUTPUT_CHANNELS = 2u;

			/** バッファサイズをバースト何個ぶんにするか。2 = 遅延と安定のバランス点 */
			constexpr int32_t BUFFER_BURST_COUNT = 2;


			/**
			 * AAudio の data callback。**オーディオスレッド**から呼ばれる。
			 *
			 * AAUDIO_FORMAT_PCM_FLOAT で開いているので audioData はインターリーブ float。
			 * `SoftwareMixer::Render` はロックも確保も IO もしないので、そのまま書かせてよい
			 * (§3.3 の実時間契約)。
			 */
			aaudio_data_callback_result_t DataCallback(AAudioStream* /*stream*/,
			                                           void*         userData,
			                                           void*         audioData,
			                                           int32_t       numFrames)
			{
				auto* backend = static_cast<AAudioSoundBackend*>(userData);
				if (backend == nullptr || audioData == nullptr || numFrames <= 0)
				{
					return AAUDIO_CALLBACK_RESULT_CONTINUE;
				}

				backend->RenderFrames(static_cast<float*>(audioData), static_cast<uint32_t>(numFrames));
				return AAUDIO_CALLBACK_RESULT_CONTINUE;
			}
		}


		AAudioSoundBackend::AAudioSoundBackend()
			: stream_(nullptr)
			, mixer_()
			, outputFormat_()
			, outputFrames_(0u)
			, latencySeconds_(0.0)
			, initialized_(false)
		{
		}


		AAudioSoundBackend::~AAudioSoundBackend()
		{
			Finalize();
		}


		bool AAudioSoundBackend::Initialize()
		{
			if (initialized_) { return true; }

			outputFormat_.sampleRate    = OUTPUT_SAMPLE_RATE;
			outputFormat_.channels      = OUTPUT_CHANNELS;
			outputFormat_.bitsPerSample = 32u;
			outputFormat_.isFloat       = true;

			if (!mixer_.Initialize(outputFormat_))
			{
				aq::StartupLog("  [sound] SoftwareMixer の初期化に失敗");
				return false;
			}

			AAudioStreamBuilder* builder = nullptr;
			if (AAudio_createStreamBuilder(&builder) != AAUDIO_OK || builder == nullptr)
			{
				aq::StartupLog("  [sound] AAudio_createStreamBuilder に失敗");
				Finalize();
				return false;
			}

			// 共有モードで開く。専有(EXCLUSIVE)は他アプリの音を止めうるのでゲームでは使わない。
			AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_OUTPUT);
			AAudioStreamBuilder_setSharingMode(builder, AAUDIO_SHARING_MODE_SHARED);
			AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_FLOAT);
			AAudioStreamBuilder_setChannelCount(builder, OUTPUT_CHANNELS);
			AAudioStreamBuilder_setSampleRate(builder, static_cast<int32_t>(OUTPUT_SAMPLE_RATE));
			AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
			AAudioStreamBuilder_setDataCallback(builder, &DataCallback, this);

			AAudioStream*         stream = nullptr;
			const aaudio_result_t opened = AAudioStreamBuilder_openStream(builder, &stream);

			// builder はストリームを開いた後は不要。成功・失敗のどちらの経路でも必ず解放する。
			AAudioStreamBuilder_delete(builder);
			builder = nullptr;

			if (opened != AAUDIO_OK || stream == nullptr)
			{
				aq::StartupLog("  [sound] AAudioStreamBuilder_openStream に失敗");
				Finalize();
				return false;
			}
			stream_ = stream;

			// AAudio は要求どおりのレート/チャンネル数で開くとは限らない。
			// 実際に開けた値でミキサを作り直さないと、ピッチずれやチャンネル取り違えになる。
			const int32_t actualSampleRate = AAudioStream_getSampleRate(stream);
			const int32_t actualChannels   = AAudioStream_getChannelCount(stream);
			if (actualSampleRate != static_cast<int32_t>(OUTPUT_SAMPLE_RATE)
			    || actualChannels != static_cast<int32_t>(OUTPUT_CHANNELS))
			{
				aq::StartupLog("  [sound] 要求と異なるフォーマットで開いたのでミキサを作り直す");

				mixer_.Finalize();

				outputFormat_.sampleRate    = static_cast<uint32_t>(actualSampleRate);
				outputFormat_.channels      = static_cast<uint16_t>(actualChannels);
				outputFormat_.bitsPerSample = 32u;
				outputFormat_.isFloat       = true;

				if (!mixer_.Initialize(outputFormat_))
				{
					aq::StartupLog("  [sound] 実フォーマットでの SoftwareMixer 初期化に失敗");
					Finalize();
					return false;
				}
			}

			// バッファはバースト 2 個ぶん。1 個だとコールバックの揺らぎで即アンダーランする。
			const int32_t framesPerBurst = AAudioStream_getFramesPerBurst(stream);
			if (framesPerBurst > 0)
			{
				AAudioStream_setBufferSizeInFrames(stream, framesPerBurst * BUFFER_BURST_COUNT);
			}

			// 推定出力遅延。「バッファに積んである未再生ぶん(bufferSizeInFrames)」＋
			// 「1 回のコールバックで先に書くぶん(framesPerBurst)」を出力レートで割った目安。
			// AAudioStream_getTimestamp() による厳密な提示時刻からの算出は P5b で対応する。
			{
				const int32_t bufferFrames = AAudioStream_getBufferSizeInFrames(stream);
				const double  totalFrames  = static_cast<double>(bufferFrames > 0 ? bufferFrames : 0)
				                           + static_cast<double>(framesPerBurst > 0 ? framesPerBurst : 0);
				latencySeconds_ = (outputFormat_.sampleRate != 0u)
				                ? totalFrames / static_cast<double>(outputFormat_.sampleRate)
				                : 0.0;
			}

			if (AAudioStream_requestStart(stream) != AAUDIO_OK)
			{
				aq::StartupLog("  [sound] AAudioStream_requestStart に失敗");
				Finalize();
				return false;
			}

			initialized_ = true;
			{
				char message[128] = {};
				std::snprintf(message, sizeof(message), "  [sound] AAudio 出力を開始 (%uHz / float32 / %uch)",
				              outputFormat_.sampleRate, static_cast<uint32_t>(outputFormat_.channels));
				aq::StartupLog(message);
			}
			return true;
		}


		void AAudioSoundBackend::Finalize()
		{
			if (stream_ != nullptr)
			{
				auto* stream = static_cast<AAudioStream*>(stream_);
				// 停止 → クローズの順。止める前に閉じると data callback が走っている
				// 最中に足元を崩すことになる。
				AAudioStream_requestStop(stream);
				AAudioStream_close(stream);
				stream_ = nullptr;
			}

			// initialized_ は見ない。Initialize がストリームを開く前に失敗しても、
			// 先に初期化したミキサをここで畳む必要があるため(SoftwareMixer::Finalize は冪等)。
			mixer_.Finalize();
			initialized_ = false;
		}


		std::unique_ptr<ISoundVoice> AAudioSoundBackend::CreateVoice(const SoundFormat& format, SoundBusId bus)
		{
			const SoftwareMixer::VoiceId voiceId = mixer_.AcquireVoice(format, bus);
			if (voiceId == SoftwareMixer::INVALID_VOICE_ID)
			{
				return nullptr;
			}
			return std::make_unique<MixerSoundVoice>(&mixer_, voiceId);
		}


		void AAudioSoundBackend::SetBusVolume(SoundBusId bus, float volume)
		{
			mixer_.SetBusVolume(bus, volume);
		}


		void AAudioSoundBackend::SetMasterVolume(float volume)
		{
			mixer_.SetMasterVolume(volume);
		}


		SoundClock AAudioSoundBackend::GetOutputClock() const
		{
			SoundClock clock;
			clock.outputFrames   = outputFrames_.load(std::memory_order_relaxed);
			clock.sampleRate     = outputFormat_.sampleRate;
			clock.latencySeconds = latencySeconds_;
			clock.underrunCount  = mixer_.GetUnderrunCount();
			return clock;
		}


		void AAudioSoundBackend::Update()
		{
			// オーディオスレッドで参照が切れた RefSoundClip の解放はここで行う(§8.3 / §3.3(e))。
			mixer_.Update();
		}


		void AAudioSoundBackend::RenderFrames(float* out, uint32_t frames)
		{
			mixer_.Render(out, frames);
			outputFrames_.fetch_add(frames, std::memory_order_relaxed);
		}
	}
}
#endif // AQ_PLATFORM_ANDROID
