#include "aq.h"
// macOS 以外では空 TU。
#if defined(AQ_PLATFORM_MAC)
#include "Sound/CoreAudio/CoreAudioSoundBackend.h"
#include "Sound/CoreAudio/CoreAudioSoundVoice.h"

#include <AudioToolbox/AudioToolbox.h>
#include <AudioUnit/AudioUnit.h>
#include <CoreAudio/CoreAudio.h>


namespace aq
{
	namespace sound
	{
		namespace
		{
			/** 出力サンプルレート。SoftwareMixer の想定(§5)と揃える */
			constexpr uint32_t OUTPUT_SAMPLE_RATE = 48000u;

			/** 出力チャンネル数(ステレオ固定) */
			constexpr uint16_t OUTPUT_CHANNELS = 2u;


			/** デバイスの UInt32 プロパティを 1 つ読む。取れなければ 0 */
			UInt32 GetDeviceUInt32(AudioObjectID device, AudioObjectPropertySelector selector)
			{
				AudioObjectPropertyAddress address{};
				address.mSelector = selector;
				address.mScope    = kAudioObjectPropertyScopeOutput;
				address.mElement  = kAudioObjectPropertyElementMain;

				UInt32 value = 0u;
				UInt32 size  = sizeof(value);
				if (AudioObjectGetPropertyData(device, &address, 0, nullptr, &size, &value) != noErr)
				{
					return 0u;
				}
				return value;
			}


			/**
			 * 出力の推定遅延 [秒] を求める。
			 *
			 * `kAudioUnitProperty_Latency` は AudioUnit **自身**の遅延しか返さず、
			 * DefaultOutput では 0 になる。A/V 同期(`SoundStream`)が欲しいのは
			 * 「書いた PCM が実際に鳴るまで」なので、HAL 側の 3 つを足す:
			 *   デバイス遅延 + セーフティオフセット + バッファ長(いずれもフレーム)。
			 * どれか取れなくても 0 として扱い、全体が 0 になっても再生自体には影響しない。
			 */
			double QueryOutputLatencySeconds(AudioComponentInstance unit)
			{
				AudioObjectID device = kAudioObjectUnknown;
				UInt32        size   = sizeof(device);
				if (AudioUnitGetProperty(unit, kAudioOutputUnitProperty_CurrentDevice,
				                         kAudioUnitScope_Global, 0, &device, &size) != noErr
				    || device == kAudioObjectUnknown)
				{
					return 0.0;
				}

				const UInt32 deviceLatency = GetDeviceUInt32(device, kAudioDevicePropertyLatency);
				const UInt32 safetyOffset  = GetDeviceUInt32(device, kAudioDevicePropertySafetyOffset);
				const UInt32 bufferFrames  = GetDeviceUInt32(device, kAudioDevicePropertyBufferFrameSize);

				// デバイスの実サンプルレート(出力レートと違うことがある)。
				Float64 deviceRate = 0.0;
				{
					AudioObjectPropertyAddress address{};
					address.mSelector = kAudioDevicePropertyNominalSampleRate;
					address.mScope    = kAudioObjectPropertyScopeOutput;
					address.mElement  = kAudioObjectPropertyElementMain;
					UInt32 rateSize   = sizeof(deviceRate);
					if (AudioObjectGetPropertyData(device, &address, 0, nullptr, &rateSize, &deviceRate) != noErr)
					{
						deviceRate = 0.0;
					}
				}
				if (deviceRate <= 0.0) { deviceRate = static_cast<Float64>(OUTPUT_SAMPLE_RATE); }

				const double totalFrames = static_cast<double>(deviceLatency)
				                         + static_cast<double>(safetyOffset)
				                         + static_cast<double>(bufferFrames);

				double seconds = totalFrames / static_cast<double>(deviceRate);

				// AudioUnit 自身の遅延も足す(通常 0 だが、取れるなら含める)。
				Float64 unitLatency = 0.0;
				UInt32  unitSize    = sizeof(unitLatency);
				if (AudioUnitGetProperty(unit, kAudioUnitProperty_Latency,
				                         kAudioUnitScope_Global, 0, &unitLatency, &unitSize) == noErr)
				{
					seconds += static_cast<double>(unitLatency);
				}
				return seconds;
			}


			/**
			 * AudioUnit のレンダーコールバック。**出力スレッド**から呼ばれる。
			 *
			 * インターリーブ float32 で開いているのでバッファは 1 本。
			 * `SoftwareMixer::Render` はロックも確保もしないので、そのまま書かせてよい。
			 */
			OSStatus RenderCallback(void*                       inRefCon,
			                        AudioUnitRenderActionFlags* /*ioActionFlags*/,
			                        const AudioTimeStamp*       /*inTimeStamp*/,
			                        UInt32                      /*inBusNumber*/,
			                        UInt32                      inNumberFrames,
			                        AudioBufferList*            ioData)
			{
				auto* backend = static_cast<CoreAudioSoundBackend*>(inRefCon);
				if (backend == nullptr || ioData == nullptr || ioData->mNumberBuffers == 0)
				{
					return noErr;
				}

				auto* out = static_cast<float*>(ioData->mBuffers[0].mData);
				if (out == nullptr)
				{
					return noErr;
				}

				backend->RenderFrames(out, static_cast<uint32_t>(inNumberFrames));
				return noErr;
			}
		}


		CoreAudioSoundBackend::CoreAudioSoundBackend()
			: outputUnit_(nullptr)
			, mixer_()
			, outputFormat_()
			, outputFrames_(0u)
			, latencySeconds_(0.0)
			, initialized_(false)
		{
		}


		CoreAudioSoundBackend::~CoreAudioSoundBackend()
		{
			Finalize();
		}


		bool CoreAudioSoundBackend::Initialize()
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

			// 既定の出力デバイス。kAudioUnitSubType_DefaultOutput は macOS 専用
			// (iOS は RemoteIO)。デバイスの切り替えは OS 側が面倒を見る。
			AudioComponentDescription desc{};
			desc.componentType         = kAudioUnitType_Output;
			desc.componentSubType      = kAudioUnitSubType_DefaultOutput;
			desc.componentManufacturer = kAudioUnitManufacturer_Apple;

			AudioComponent component = AudioComponentFindNext(nullptr, &desc);
			if (component == nullptr)
			{
				aq::StartupLog("  [sound] 既定の出力 AudioUnit が見つからない");
				return false;
			}

			AudioComponentInstance unit = nullptr;
			if (AudioComponentInstanceNew(component, &unit) != noErr || unit == nullptr)
			{
				aq::StartupLog("  [sound] AudioComponentInstanceNew に失敗");
				return false;
			}
			outputUnit_ = unit;

			// インターリーブ float32。kAudioFormatFlagIsNonInterleaved を立てないことで
			// 「1 バッファに L R L R ...」になり、ミキサの出力をそのまま書ける。
			AudioStreamBasicDescription format{};
			format.mSampleRate       = static_cast<Float64>(OUTPUT_SAMPLE_RATE);
			format.mFormatID         = kAudioFormatLinearPCM;
			format.mFormatFlags      = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
			format.mChannelsPerFrame = OUTPUT_CHANNELS;
			format.mBitsPerChannel   = 32u;
			format.mFramesPerPacket  = 1u;
			format.mBytesPerFrame    = format.mChannelsPerFrame * sizeof(float);
			format.mBytesPerPacket   = format.mBytesPerFrame * format.mFramesPerPacket;

			if (AudioUnitSetProperty(unit, kAudioUnitProperty_StreamFormat,
			                         kAudioUnitScope_Input, 0, &format, sizeof(format)) != noErr)
			{
				aq::StartupLog("  [sound] 出力フォーマット(48kHz/float32/2ch interleaved)を設定できない");
				Finalize();
				return false;
			}

			AURenderCallbackStruct callback{};
			callback.inputProc       = &RenderCallback;
			callback.inputProcRefCon = this;
			if (AudioUnitSetProperty(unit, kAudioUnitProperty_SetRenderCallback,
			                         kAudioUnitScope_Input, 0, &callback, sizeof(callback)) != noErr)
			{
				aq::StartupLog("  [sound] レンダーコールバックを設定できない");
				Finalize();
				return false;
			}

			if (AudioUnitInitialize(unit) != noErr)
			{
				aq::StartupLog("  [sound] AudioUnitInitialize に失敗");
				Finalize();
				return false;
			}

			// 出力遅延。取れなくても再生自体はできるので失敗は致命ではない。
			latencySeconds_ = QueryOutputLatencySeconds(unit);

			if (AudioOutputUnitStart(unit) != noErr)
			{
				aq::StartupLog("  [sound] AudioOutputUnitStart に失敗");
				Finalize();
				return false;
			}

			initialized_ = true;
			aq::StartupLog("  [sound] CoreAudio 出力を開始 (48kHz / float32 / 2ch)");
			return true;
		}


		void CoreAudioSoundBackend::Finalize()
		{
			if (outputUnit_ != nullptr)
			{
				auto unit = static_cast<AudioComponentInstance>(outputUnit_);
				// 停止 → Uninitialize → Dispose の順。停止前に破棄するとコールバックが
				// 走っている最中に足元を崩すことになる。
				AudioOutputUnitStop(unit);
				AudioUnitUninitialize(unit);
				AudioComponentInstanceDispose(unit);
				outputUnit_ = nullptr;
			}

			if (initialized_)
			{
				mixer_.Finalize();
				initialized_ = false;
			}
		}


		std::unique_ptr<ISoundVoice> CoreAudioSoundBackend::CreateVoice(const SoundFormat& format, SoundBusId bus)
		{
			const SoftwareMixer::VoiceId voiceId = mixer_.AcquireVoice(format, bus);
			if (voiceId == SoftwareMixer::INVALID_VOICE_ID)
			{
				return nullptr;
			}
			return std::make_unique<CoreAudioSoundVoice>(&mixer_, voiceId);
		}


		void CoreAudioSoundBackend::SetBusVolume(SoundBusId bus, float volume)
		{
			mixer_.SetBusVolume(bus, volume);
		}


		void CoreAudioSoundBackend::SetMasterVolume(float volume)
		{
			mixer_.SetMasterVolume(volume);
		}


		SoundClock CoreAudioSoundBackend::GetOutputClock() const
		{
			SoundClock clock;
			clock.outputFrames   = outputFrames_.load(std::memory_order_relaxed);
			clock.sampleRate     = outputFormat_.sampleRate;
			clock.latencySeconds = latencySeconds_;
			clock.underrunCount  = mixer_.GetUnderrunCount();
			return clock;
		}


		void CoreAudioSoundBackend::Update()
		{
			// 出力スレッドで参照が切れた RefSoundClip の解放はここで行う(§5 / architecture.md §5)。
			mixer_.Update();
		}


		void CoreAudioSoundBackend::RenderFrames(float* out, uint32_t frames)
		{
			mixer_.Render(out, frames);
			outputFrames_.fetch_add(frames, std::memory_order_relaxed);
		}
	}
}
#endif // AQ_PLATFORM_MAC
