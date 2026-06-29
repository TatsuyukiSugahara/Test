#include "aq.h"
#include "WavStreamDecoder.h"
#include <cstring>


namespace aq
{
	namespace sound
	{
		namespace
		{
			uint16_t ReadU16(const uint8_t* p)
			{
				uint16_t v = 0;
				std::memcpy(&v, p, sizeof(v));
				return v;
			}
			uint32_t ReadU32(const uint8_t* p)
			{
				uint32_t v = 0;
				std::memcpy(&v, p, sizeof(v));
				return v;
			}
			bool TagEquals(const uint8_t* p, const char* tag)
			{
				return std::memcmp(p, tag, 4) == 0;
			}

			constexpr uint16_t TAG_PCM        = 0x0001;
			constexpr uint16_t TAG_IEEE_FLOAT = 0x0003;
			constexpr uint16_t TAG_EXTENSIBLE = 0xFFFE;
		}


		WavStreamDecoder::~WavStreamDecoder()
		{
			Close();
		}


		void WavStreamDecoder::Close()
		{
			if (file_) {
				std::fclose(file_);
				file_ = nullptr;
			}
		}


		bool WavStreamDecoder::Open(const char* path)
		{
			Close();
			if (path == nullptr) {
				return false;
			}
			if (fopen_s(&file_, path, "rb") != 0 || file_ == nullptr) {
				return false;
			}

			uint8_t header[12] = {};
			if (std::fread(header, 1, 12, file_) != 12u
				|| !TagEquals(header, "RIFF") || !TagEquals(header + 8, "WAVE")) {
				Close();
				return false;
			}

			bool haveFmt  = false;
			bool haveData = false;

			// チャンク走査。fmt は本体を読み、data は位置だけ記録してストリームする。
			uint8_t chunkHeader[8] = {};
			while (std::fread(chunkHeader, 1, 8, file_) == 8u)
			{
				const uint32_t chunkSize = ReadU32(chunkHeader + 4);

				if (TagEquals(chunkHeader, "fmt "))
				{
					uint8_t fmt[40] = {};
					const uint32_t toRead = chunkSize < sizeof(fmt) ? chunkSize : static_cast<uint32_t>(sizeof(fmt));
					if (chunkSize < 16u || std::fread(fmt, 1, toRead, file_) != toRead) {
						Close();
						return false;
					}
					uint16_t formatTag    = ReadU16(fmt + 0);
					format_.channels      = ReadU16(fmt + 2);
					format_.sampleRate    = ReadU32(fmt + 4);
					format_.bitsPerSample = ReadU16(fmt + 14);
					if (formatTag == TAG_EXTENSIBLE && chunkSize >= 40u) {
						formatTag = ReadU16(fmt + 24);
					}
					format_.isFloat = (formatTag == TAG_IEEE_FLOAT);
					if (formatTag != TAG_PCM && formatTag != TAG_IEEE_FLOAT) {
						Close();
						return false;
					}
					haveFmt = true;

					// fmt 本体の残り（パディング含む）をスキップ
					const long skip = static_cast<long>(chunkSize - toRead) + static_cast<long>(chunkSize & 1u);
					if (skip > 0) {
						std::fseek(file_, skip, SEEK_CUR);
					}
				}
				else if (TagEquals(chunkHeader, "data"))
				{
					dataOffset_ = std::ftell(file_);
					dataBytes_  = chunkSize;
					haveData    = true;
					break;
				}
				else
				{
					// 不要チャンクは本体（偶数境界）をスキップ
					std::fseek(file_, static_cast<long>(chunkSize + (chunkSize & 1u)), SEEK_CUR);
				}
			}

			if (!haveFmt || !haveData || !format_.IsValid()) {
				Close();
				return false;
			}

			readBytes_ = 0;
			std::fseek(file_, dataOffset_, SEEK_SET);
			return true;
		}


		uint64_t WavStreamDecoder::GetTotalFrames() const
		{
			const uint32_t bpf = format_.BytesPerFrame();
			return bpf == 0u ? 0u : dataBytes_ / bpf;
		}


		uint32_t WavStreamDecoder::ReadFrames(void* dst, uint32_t maxFrames)
		{
			if (file_ == nullptr || dst == nullptr) {
				return 0u;
			}
			const uint32_t bpf = format_.BytesPerFrame();
			if (bpf == 0u) {
				return 0u;
			}

			const uint32_t remainBytes = dataBytes_ - readBytes_;
			uint32_t       framesToRead = remainBytes / bpf;
			if (framesToRead > maxFrames) {
				framesToRead = maxFrames;
			}
			if (framesToRead == 0u) {
				return 0u;
			}

			const uint32_t wantBytes = framesToRead * bpf;
			const size_t   gotBytes  = std::fread(dst, 1, wantBytes, file_);
			readBytes_ += static_cast<uint32_t>(gotBytes);
			return static_cast<uint32_t>(gotBytes / bpf);
		}


		bool WavStreamDecoder::Seek(uint64_t frame)
		{
			if (file_ == nullptr) {
				return false;
			}
			const uint32_t bpf = format_.BytesPerFrame();
			if (bpf == 0u) {
				return false;
			}

			uint64_t byteOffset = frame * bpf;
			if (byteOffset > dataBytes_) {
				byteOffset = dataBytes_;
			}
			if (std::fseek(file_, dataOffset_ + static_cast<long>(byteOffset), SEEK_SET) != 0) {
				return false;
			}
			readBytes_ = static_cast<uint32_t>(byteOffset);
			return true;
		}
	}
}
