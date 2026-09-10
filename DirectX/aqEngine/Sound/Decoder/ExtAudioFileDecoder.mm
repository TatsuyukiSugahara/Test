#include "aq.h"
// macOS 以外では空 TU。
#if defined(AQ_PLATFORM_MAC)
#include "Sound/Decoder/ExtAudioFileDecoder.h"

#include <AudioToolbox/AudioToolbox.h>
#include <CoreFoundation/CoreFoundation.h>


namespace aq
{
	namespace sound
	{
		namespace
		{
			/** 出力する PCM のビット深度。MFDecoder と揃える(16bit 符号付き整数) */
			constexpr uint16_t OUTPUT_BITS_PER_SAMPLE = 16u;


			/**
			 * ExtAudioFile を開いて「クライアントフォーマット」を 16bit 整数 PCM に設定する。
			 * チャンネル数とサンプルレートは**ファイル本来の値をそのまま使う**
			 * (リサンプルは SoftwareMixer が担当するため、ここでは変換しない)。
			 */
			ExtAudioFileRef OpenAndConfigure(const char* path, SoundFormat& outFormat, uint64_t& outTotalFrames)
			{
				if (path == nullptr || path[0] == '\0') { return nullptr; }

				CFURLRef url = CFURLCreateFromFileSystemRepresentation(
					kCFAllocatorDefault,
					reinterpret_cast<const UInt8*>(path),
					static_cast<CFIndex>(std::strlen(path)),
					false);
				if (url == nullptr) { return nullptr; }

				ExtAudioFileRef file = nullptr;
				const OSStatus openStatus = ExtAudioFileOpenURL(url, &file);
				CFRelease(url);
				if (openStatus != noErr || file == nullptr) { return nullptr; }

				// ファイル本来のフォーマット(チャンネル数とサンプルレートを引き継ぐため)。
				AudioStreamBasicDescription fileFormat{};
				UInt32 size = sizeof(fileFormat);
				if (ExtAudioFileGetProperty(file, kExtAudioFileProperty_FileDataFormat,
				                            &size, &fileFormat) != noErr)
				{
					ExtAudioFileDispose(file);
					return nullptr;
				}

				const uint16_t channels = static_cast<uint16_t>(fileFormat.mChannelsPerFrame);
				if (channels == 0u)
				{
					ExtAudioFileDispose(file);
					return nullptr;
				}

				// 読み出し形式。インターリーブの 16bit 整数へ変換させる。
				AudioStreamBasicDescription clientFormat{};
				clientFormat.mSampleRate       = fileFormat.mSampleRate;
				clientFormat.mFormatID         = kAudioFormatLinearPCM;
				clientFormat.mFormatFlags      = kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked;
				clientFormat.mChannelsPerFrame = channels;
				clientFormat.mBitsPerChannel   = OUTPUT_BITS_PER_SAMPLE;
				clientFormat.mFramesPerPacket  = 1u;
				clientFormat.mBytesPerFrame    = channels * (OUTPUT_BITS_PER_SAMPLE / 8u);
				clientFormat.mBytesPerPacket   = clientFormat.mBytesPerFrame;

				if (ExtAudioFileSetProperty(file, kExtAudioFileProperty_ClientDataFormat,
				                            sizeof(clientFormat), &clientFormat) != noErr)
				{
					ExtAudioFileDispose(file);
					return nullptr;
				}

				SInt64 frames = 0;
				size = sizeof(frames);
				if (ExtAudioFileGetProperty(file, kExtAudioFileProperty_FileLengthFrames,
				                            &size, &frames) != noErr)
				{
					frames = 0;
				}

				outFormat.sampleRate    = static_cast<uint32_t>(clientFormat.mSampleRate);
				outFormat.channels      = channels;
				outFormat.bitsPerSample = OUTPUT_BITS_PER_SAMPLE;
				outFormat.isFloat       = false;
				outTotalFrames          = (frames > 0) ? static_cast<uint64_t>(frames) : 0u;
				return file;
			}
		}


		ExtAudioFileDecoder::~ExtAudioFileDecoder()
		{
			Close();
		}


		void ExtAudioFileDecoder::Close()
		{
			if (file_ != nullptr)
			{
				ExtAudioFileDispose(static_cast<ExtAudioFileRef>(file_));
				file_ = nullptr;
			}
			format_      = SoundFormat{};
			totalFrames_ = 0u;
			readFrames_  = 0u;
			atEnd_       = false;
		}


		bool ExtAudioFileDecoder::Open(const char* path)
		{
			Close();
			file_ = OpenAndConfigure(path, format_, totalFrames_);
			if (file_ == nullptr)
			{
				format_ = SoundFormat{};
				return false;
			}
			return true;
		}


		uint32_t ExtAudioFileDecoder::ReadFrames(void* dst, uint32_t maxFrames)
		{
			if (file_ == nullptr || dst == nullptr || maxFrames == 0u || atEnd_)
			{
				return 0u;
			}

			AudioBufferList bufferList{};
			bufferList.mNumberBuffers              = 1u;
			bufferList.mBuffers[0].mNumberChannels = format_.channels;
			bufferList.mBuffers[0].mDataByteSize   = maxFrames * format_.BytesPerFrame();
			bufferList.mBuffers[0].mData           = dst;

			UInt32 frames = maxFrames;
			if (ExtAudioFileRead(static_cast<ExtAudioFileRef>(file_), &frames, &bufferList) != noErr)
			{
				atEnd_ = true;
				return 0u;
			}

			// frames == 0 は EOF(ExtAudioFileRead はエラーを返さずに 0 を返す)。
			if (frames == 0u)
			{
				atEnd_ = true;
				return 0u;
			}

			readFrames_ += frames;
			return static_cast<uint32_t>(frames);
		}


		bool ExtAudioFileDecoder::Seek(uint64_t frame)
		{
			if (file_ == nullptr) { return false; }
			if (ExtAudioFileSeek(static_cast<ExtAudioFileRef>(file_), static_cast<SInt64>(frame)) != noErr)
			{
				return false;
			}
			readFrames_ = frame;
			atEnd_      = false;
			return true;
		}


		bool ExtAudioFileDecoder::DecodeFileFully(const char* path, SoundFormat& outFormat,
		                                          std::vector<uint8_t>& outPcm)
		{
			outFormat = SoundFormat{};
			outPcm.clear();

			uint64_t        totalFrames = 0u;
			ExtAudioFileRef file        = OpenAndConfigure(path, outFormat, totalFrames);
			if (file == nullptr)
			{
				outFormat = SoundFormat{};
				return false;
			}

			const uint32_t bytesPerFrame = outFormat.BytesPerFrame();
			// 総フレーム数が取れていれば一度で確保する。取れない形式でも下のループが伸ばす。
			if (totalFrames > 0u)
			{
				outPcm.reserve(static_cast<size_t>(totalFrames) * bytesPerFrame);
			}

			// 1 回の読み出しフレーム数。大きすぎても意味が無いので固定の塊で回す。
			constexpr uint32_t CHUNK_FRAMES = 8192u;
			std::vector<uint8_t> chunk(static_cast<size_t>(CHUNK_FRAMES) * bytesPerFrame);

			for (;;)
			{
				AudioBufferList bufferList{};
				bufferList.mNumberBuffers              = 1u;
				bufferList.mBuffers[0].mNumberChannels = outFormat.channels;
				bufferList.mBuffers[0].mDataByteSize   = static_cast<UInt32>(chunk.size());
				bufferList.mBuffers[0].mData           = chunk.data();

				UInt32 frames = CHUNK_FRAMES;
				if (ExtAudioFileRead(file, &frames, &bufferList) != noErr)
				{
					ExtAudioFileDispose(file);
					outFormat = SoundFormat{};
					outPcm.clear();
					return false;
				}
				if (frames == 0u) { break; }   // EOF

				const size_t byteCount = static_cast<size_t>(frames) * bytesPerFrame;
				outPcm.insert(outPcm.end(), chunk.begin(), chunk.begin() + byteCount);
			}

			ExtAudioFileDispose(file);
			return !outPcm.empty();
		}
	}
}
#endif // AQ_PLATFORM_MAC
