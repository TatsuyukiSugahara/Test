#include "aq.h"
#include "WavDecoder.h"
#include <cstdio>


namespace aq
{
	namespace sound
	{
		namespace
		{
			// リトルエンディアン読み出し（Windows は LE だが memcpy で意図を明示）。
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

			// WAVE フォーマットタグ（WAVE_FORMAT_* は windows のマクロなので別名で定義）
			constexpr uint16_t TAG_PCM        = 0x0001;
			constexpr uint16_t TAG_IEEE_FLOAT = 0x0003;
			constexpr uint16_t TAG_EXTENSIBLE = 0xFFFE;
		}


		bool WavDecoder::DecodeMemory(const uint8_t* data, size_t size, SoundFormat& outFormat, std::vector<uint8_t>& outPcm)
		{
			// 最低限 RIFF ヘッダ（12 バイト）が必要
			if (data == nullptr || size < 12u) {
				return false;
			}
			if (!TagEquals(data, "RIFF") || !TagEquals(data + 8, "WAVE")) {
				return false;
			}

			bool        haveFmt  = false;
			SoundFormat format;
			const uint8_t* dataChunk = nullptr;
			uint32_t       dataSize  = 0;

			// チャンク走査（12 バイト目以降）
			size_t offset = 12u;
			while (offset + 8u <= size)
			{
				const uint8_t* chunkId = data + offset;
				const uint32_t chunkSize = ReadU32(data + offset + 4u);
				const size_t   body      = offset + 8u;
				if (body + chunkSize > size) {
					break;   // 壊れたチャンク
				}

				if (TagEquals(chunkId, "fmt "))
				{
					if (chunkSize < 16u) {
						return false;
					}
					const uint8_t* f = data + body;
					uint16_t formatTag    = ReadU16(f + 0);
					format.channels       = ReadU16(f + 2);
					format.sampleRate     = ReadU32(f + 4);
					format.bitsPerSample  = ReadU16(f + 14);

					// EXTENSIBLE はサブフォーマット GUID 先頭 2 バイトで実体を判定
					if (formatTag == TAG_EXTENSIBLE && chunkSize >= 40u) {
						formatTag = ReadU16(f + 24);
					}
					format.isFloat = (formatTag == TAG_IEEE_FLOAT);

					if (formatTag != TAG_PCM && formatTag != TAG_IEEE_FLOAT) {
						return false;   // 未対応（圧縮 WAV 等）
					}
					haveFmt = true;
				}
				else if (TagEquals(chunkId, "data"))
				{
					dataChunk = data + body;
					dataSize  = chunkSize;
				}

				// チャンクは偶数境界へパディングされる
				offset = body + chunkSize + (chunkSize & 1u);
			}

			if (!haveFmt || dataChunk == nullptr || !format.IsValid()) {
				return false;
			}

			outFormat = format;
			outPcm.assign(dataChunk, dataChunk + dataSize);
			return true;
		}


		bool WavDecoder::DecodeFile(const char* path, SoundFormat& outFormat, std::vector<uint8_t>& outPcm)
		{
			if (path == nullptr) {
				return false;
			}

			FILE* fp = nullptr;
			if (fopen_s(&fp, path, "rb") != 0 || fp == nullptr) {
				return false;
			}

			std::fseek(fp, 0, SEEK_END);
			long fileSize = std::ftell(fp);
			std::fseek(fp, 0, SEEK_SET);
			if (fileSize <= 0) {
				std::fclose(fp);
				return false;
			}

			std::vector<uint8_t> raw(static_cast<size_t>(fileSize));
			size_t read = std::fread(raw.data(), 1, raw.size(), fp);
			std::fclose(fp);
			if (read != raw.size()) {
				return false;
			}

			return DecodeMemory(raw.data(), raw.size(), outFormat, outPcm);
		}
	}
}
