#include "aq.h"
#include "MFDecoder.h"
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <propvarutil.h>
#include <objbase.h>
#include <wrl/client.h>
#include <cstring>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "propsys.lib")


namespace aq
{
	namespace sound
	{
		using Microsoft::WRL::ComPtr;

		namespace
		{
			std::wstring ToWide(const char* utf8)
			{
				if (utf8 == nullptr) {
					return std::wstring();
				}
				const int len = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, nullptr, 0);
				if (len <= 0) {
					return std::wstring();
				}
				std::wstring wide(static_cast<size_t>(len - 1), L'\0');
				MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wide.data(), len);
				return wide;
			}
		}


		MFDecoder::~MFDecoder()
		{
			Close();
		}


		void MFDecoder::Close()
		{
			if (reader_) {
				reader_->Release();
				reader_ = nullptr;
			}
			pending_.clear();
			pendingOffset_ = 0;
			if (mfStarted_) {
				MFShutdown();
				mfStarted_ = false;
			}
			if (comInitialized_) {
				CoUninitialize();
				comInitialized_ = false;
			}
		}


		bool MFDecoder::Open(const char* path)
		{
			Close();

			// ワーカースレッドでも安全なよう COM/MF を自前初期化する。
			const HRESULT hrCom = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
			comInitialized_ = SUCCEEDED(hrCom);   // RPC_E_CHANGED_MODE は失敗扱い（balance しない）

			if (FAILED(MFStartup(MF_VERSION, MFSTARTUP_LITE))) {
				Close();
				return false;
			}
			mfStarted_ = true;

			const std::wstring widePath = ToWide(path);
			if (widePath.empty()) {
				Close();
				return false;
			}

			if (FAILED(MFCreateSourceReaderFromURL(widePath.c_str(), nullptr, &reader_))) {
				Close();
				return false;
			}

			// 最初のオーディオストリームのみ選択する。
			reader_->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS), FALSE);
			reader_->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), TRUE);

			// 出力を非圧縮 PCM に強制する。
			ComPtr<IMFMediaType> pcmType;
			if (FAILED(MFCreateMediaType(pcmType.GetAddressOf()))) {
				Close();
				return false;
			}
			pcmType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
			pcmType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
			if (FAILED(reader_->SetCurrentMediaType(
					static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), nullptr, pcmType.Get()))) {
				Close();
				return false;
			}

			// 実際に確定した出力フォーマットを取得する。
			ComPtr<IMFMediaType> actualType;
			if (FAILED(reader_->GetCurrentMediaType(
					static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), &actualType))) {
				Close();
				return false;
			}
			UINT32 channels = 0, sampleRate = 0, bits = 0;
			actualType->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &channels);
			actualType->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &sampleRate);
			actualType->GetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, &bits);

			format_.channels      = static_cast<uint16_t>(channels);
			format_.sampleRate    = sampleRate;
			format_.bitsPerSample = static_cast<uint16_t>(bits);
			format_.isFloat       = false;
			if (!format_.IsValid()) {
				Close();
				return false;
			}

			// 総フレーム数（再生時間から推定。取れなければ 0）。
			PROPVARIANT durationProp;
			PropVariantInit(&durationProp);
			if (SUCCEEDED(reader_->GetPresentationAttribute(
					static_cast<DWORD>(MF_SOURCE_READER_MEDIASOURCE), MF_PD_DURATION, &durationProp))) {
				const uint64_t hundredNs = durationProp.uhVal.QuadPart;   // 100ns 単位
				totalFrames_ = hundredNs * sampleRate / 10000000ull;
			}
			PropVariantClear(&durationProp);

			atEnd_ = false;
			return true;
		}


		bool MFDecoder::FillPending()
		{
			if (reader_ == nullptr) {
				return false;
			}

			DWORD            streamFlags = 0;
			ComPtr<IMFSample> sample;
			const HRESULT hr = reader_->ReadSample(
				static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), 0,
				nullptr, &streamFlags, nullptr, sample.GetAddressOf());
			if (FAILED(hr)) {
				atEnd_ = true;
				return false;
			}
			if (streamFlags & MF_SOURCE_READERF_ENDOFSTREAM) {
				atEnd_ = true;
			}
			if (sample == nullptr) {
				return false;   // EOS でサンプルなし、または gap
			}

			ComPtr<IMFMediaBuffer> buffer;
			if (FAILED(sample->ConvertToContiguousBuffer(buffer.GetAddressOf()))) {
				return false;
			}

			BYTE*  data   = nullptr;
			DWORD  length = 0;
			if (FAILED(buffer->Lock(&data, nullptr, &length))) {
				return false;
			}
			pending_.assign(data, data + length);
			pendingOffset_ = 0;
			buffer->Unlock();
			return length > 0;
		}


		uint32_t MFDecoder::ReadFrames(void* dst, uint32_t maxFrames)
		{
			if (reader_ == nullptr || dst == nullptr) {
				return 0;
			}
			const uint32_t bpf = format_.BytesPerFrame();
			if (bpf == 0u) {
				return 0;
			}

			uint8_t*       out      = static_cast<uint8_t*>(dst);
			const uint32_t wantBytes = maxFrames * bpf;
			uint32_t       written   = 0;

			while (written < wantBytes)
			{
				if (pendingOffset_ >= pending_.size()) {
					if (atEnd_) {
						break;
					}
					if (!FillPending()) {
						if (atEnd_) break;
						continue;
					}
				}

				const size_t available = pending_.size() - pendingOffset_;
				const size_t want      = wantBytes - written;
				const size_t copy      = available < want ? available : want;
				std::memcpy(out + written, pending_.data() + pendingOffset_, copy);
				pendingOffset_ += copy;
				written        += static_cast<uint32_t>(copy);
			}

			return written / bpf;
		}


		bool MFDecoder::Seek(uint64_t frame)
		{
			if (reader_ == nullptr || format_.sampleRate == 0u) {
				return false;
			}

			PROPVARIANT pos;
			PropVariantInit(&pos);
			pos.vt = VT_I8;
			pos.hVal.QuadPart = static_cast<LONGLONG>(frame * 10000000ull / format_.sampleRate);   // 100ns
			const HRESULT hr = reader_->SetCurrentPosition(GUID_NULL, pos);
			PropVariantClear(&pos);

			pending_.clear();
			pendingOffset_ = 0;
			atEnd_         = false;
			return SUCCEEDED(hr);
		}


		bool MFDecoder::DecodeFileFully(const char* path, SoundFormat& outFormat, std::vector<uint8_t>& outPcm)
		{
			MFDecoder decoder;
			if (!decoder.Open(path)) {
				return false;
			}
			outFormat = decoder.GetFormat();

			const uint32_t bpf = outFormat.BytesPerFrame();
			if (bpf == 0u) {
				return false;
			}

			outPcm.clear();
			// 既知の総フレーム数があれば予約する。
			if (decoder.GetTotalFrames() > 0) {
				outPcm.reserve(static_cast<size_t>(decoder.GetTotalFrames()) * bpf);
			}

			constexpr uint32_t CHUNK_FRAMES = 8192;
			std::vector<uint8_t> chunk(static_cast<size_t>(CHUNK_FRAMES) * bpf);
			for (;;)
			{
				const uint32_t frames = decoder.ReadFrames(chunk.data(), CHUNK_FRAMES);
				if (frames == 0) {
					break;
				}
				outPcm.insert(outPcm.end(), chunk.data(), chunk.data() + static_cast<size_t>(frames) * bpf);
			}

			return !outPcm.empty();
		}
	}
}
