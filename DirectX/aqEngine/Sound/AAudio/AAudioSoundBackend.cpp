#include "aq.h"
// Android 以外では空 TU。
#if defined(AQ_PLATFORM_ANDROID)
#include "Sound/AAudio/AAudioSoundBackend.h"
#include "Sound/Mixer/MixerSoundVoice.h"

#include <aaudio/AAudio.h>
#include <ctime>


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

			/** 状態遷移の待ち時間 [ns]。pause の完了待ちにしか使わないので短くてよい */
			constexpr int64_t STATE_CHANGE_TIMEOUT_NS = 100 * 1000 * 1000;   // 100ms


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


			/**
			 * AAudio のエラーコールバック。**AAudio 内部のスレッド**から呼ばれる。
			 *
			 * イヤホンの抜き差しのようにデバイスが切り替わるとストリームが切断され、ここへ来る。
			 * このスレッドから `AAudioStream_close` を呼んではいけない決まりなので、
			 * 印を付けるだけにして作り直しはサウンドスレッドへ回す。
			 */
			void ErrorCallback(AAudioStream* /*stream*/, void* userData, aaudio_result_t /*error*/)
			{
				auto* backend = static_cast<AAudioSoundBackend*>(userData);
				if (backend != nullptr)
				{
					backend->NotifyStreamLost();
				}
			}
		}


		AAudioSoundBackend::AAudioSoundBackend()
			: stream_(nullptr)
			, mixer_()
			, outputFormat_()
			, outputFrames_(0u)
			, latencySeconds_(0.0)
			, streamLost_(false)
			, suspended_(false)
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

			if (!OpenAndStartStream())
			{
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


		bool AAudioSoundBackend::OpenAndStartStream()
		{
			AAudioStreamBuilder* builder = nullptr;
			if (AAudio_createStreamBuilder(&builder) != AAUDIO_OK || builder == nullptr)
			{
				aq::StartupLog("  [sound] AAudio_createStreamBuilder に失敗");
				return false;
			}

			// 共有モードで開く。専有(EXCLUSIVE)は他アプリの音を止めうるのでゲームでは使わない。
			AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_OUTPUT);
			AAudioStreamBuilder_setSharingMode(builder, AAUDIO_SHARING_MODE_SHARED);
			AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_FLOAT);
			AAudioStreamBuilder_setChannelCount(builder, static_cast<int32_t>(outputFormat_.channels));
			AAudioStreamBuilder_setSampleRate(builder, static_cast<int32_t>(outputFormat_.sampleRate));
			AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
			AAudioStreamBuilder_setDataCallback(builder, &DataCallback, this);
			AAudioStreamBuilder_setErrorCallback(builder, &ErrorCallback, this);

			AAudioStream*         stream = nullptr;
			const aaudio_result_t opened = AAudioStreamBuilder_openStream(builder, &stream);

			// builder はストリームを開いた後は不要。成功・失敗のどちらの経路でも必ず解放する。
			AAudioStreamBuilder_delete(builder);
			builder = nullptr;

			if (opened != AAUDIO_OK || stream == nullptr)
			{
				aq::StartupLog("  [sound] AAudioStreamBuilder_openStream に失敗");
				return false;
			}
			stream_ = stream;

			// AAudio は要求どおりのレート/チャンネル数で開くとは限らない。
			// 実際に開けた値でミキサを作り直さないと、ピッチずれやチャンネル取り違えになる。
			const int32_t actualSampleRate = AAudioStream_getSampleRate(stream);
			const int32_t actualChannels   = AAudioStream_getChannelCount(stream);
			if (actualSampleRate != static_cast<int32_t>(outputFormat_.sampleRate)
			    || actualChannels != static_cast<int32_t>(outputFormat_.channels))
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
			// 実測が取れる状況では GetOutputClock がタイムスタンプから出し直す。
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
				return false;
			}
			return true;
		}


		void AAudioSoundBackend::CloseStream()
		{
			if (stream_ == nullptr) { return; }

			auto* stream = static_cast<AAudioStream*>(stream_);
			// 停止 → クローズの順。止める前に閉じると data callback が走っている
			// 最中に足元を崩すことになる。
			AAudioStream_requestStop(stream);
			AAudioStream_close(stream);
			stream_ = nullptr;
		}


		void AAudioSoundBackend::Finalize()
		{
			CloseStream();

			// initialized_ は見ない。Initialize がストリームを開く前に失敗しても、
			// 先に初期化したミキサをここで畳む必要があるため(SoftwareMixer::Finalize は冪等)。
			mixer_.Finalize();
			initialized_ = false;
			suspended_   = false;
			streamLost_.store(false, std::memory_order_relaxed);
		}


		void AAudioSoundBackend::OnSuspend()
		{
			if (suspended_) { return; }
			suspended_ = true;

			if (stream_ == nullptr) { return; }
			auto* stream = static_cast<AAudioStream*>(stream_);

			// 一時停止したうえで積んである分を捨てる。捨てないと復帰の瞬間に、
			// 背面に回る直前の音が鳴り直して二重に聞こえる。
			// Flush は PAUSED でしか通らないので、状態遷移の完了を待ってから呼ぶ。
			if (AAudioStream_requestPause(stream) != AAUDIO_OK) { return; }

			aaudio_stream_state_t state = AAUDIO_STREAM_STATE_UNKNOWN;
			AAudioStream_waitForStateChange(stream, AAUDIO_STREAM_STATE_PAUSING,
			                                &state, STATE_CHANGE_TIMEOUT_NS);
			if (state == AAUDIO_STREAM_STATE_PAUSED)
			{
				AAudioStream_requestFlush(stream);
			}
		}


		void AAudioSoundBackend::OnResume()
		{
			if (!suspended_) { return; }
			suspended_ = false;

			if (stream_ != nullptr)
			{
				AAudioStream_requestStart(static_cast<AAudioStream*>(stream_));
			}
		}


		void AAudioSoundBackend::NotifyStreamLost()
		{
			// エラーコールバックのスレッドから閉じてはいけない決まりなので、
			// 印を付けるだけにして Update(サウンドスレッド)へ作り直させる。
			streamLost_.store(true, std::memory_order_release);
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

			if (stream_ == nullptr || outputFormat_.sampleRate == 0u)
			{
				return clock;
			}
			auto* stream = static_cast<AAudioStream*>(stream_);

			// 提示済みのフレーム位置が取れるなら、そこから実測の遅延を出す。
			// 「書いた総数 - 実際に鳴った位置」が、まだパイプラインに残っているぶん。
			// 開いた直後や停止中は取れない(エラーが返る)ので、その場合は見積りのまま使う。
			int64_t framePosition   = 0;
			int64_t timeNanoseconds = 0;
			if (AAudioStream_getTimestamp(stream, CLOCK_MONOTONIC, &framePosition, &timeNanoseconds) == AAUDIO_OK)
			{
				const int64_t written = AAudioStream_getFramesWritten(stream);
				const int64_t pending = written - framePosition;
				if (pending > 0)
				{
					clock.latencySeconds = static_cast<double>(pending)
					                     / static_cast<double>(outputFormat_.sampleRate);
				}
			}

			// デバイス側が取りこぼした回数。ミキサ側の枯渇とは原因が別だが、
			// どちらも「音が途切れた回数」なので累積として合算して報告する。
			const int32_t xrunCount = AAudioStream_getXRunCount(stream);
			if (xrunCount > 0)
			{
				clock.underrunCount += static_cast<uint64_t>(xrunCount);
			}
			return clock;
		}


		void AAudioSoundBackend::Update()
		{
			// デバイス切替などでストリームが切れていたら、このスレッドで作り直す。
			// ミキサには触らないので、再生中のボイスはそのまま続きから鳴る。
			if (streamLost_.exchange(false, std::memory_order_acquire))
			{
				aq::StartupLog("  [sound] 出力ストリームが切れたので作り直す");
				CloseStream();

				// 作り直した直後は再生中の状態になる。背面で切れていたなら、
				// 復帰の通知が来るまで鳴らしてはいけないので止め直す。
				const bool wasSuspended = suspended_;
				suspended_ = false;

				if (OpenAndStartStream())
				{
					if (wasSuspended) { OnSuspend(); }
				}
				else
				{
					aq::StartupLog("  [sound] 作り直しに失敗。次のフレームで再試行する");
					suspended_ = wasSuspended;
					streamLost_.store(true, std::memory_order_release);
				}
			}

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
