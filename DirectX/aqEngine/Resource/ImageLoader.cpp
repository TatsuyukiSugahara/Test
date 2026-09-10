#include "aq.h"
#include "ImageLoader.h"

#include <cctype>
#include <cstdlib>

#if defined(AQ_PLATFORM_MAC)
// stb_image の実体はこの TU だけで生成する。
// Windows は WIC を使うため実体化しない (未使用コードと警告を持ち込まないため)。
#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"
#endif // AQ_PLATFORM_MAC


namespace aq
{
	namespace res
	{
		namespace
		{
			/** ワイド文字パスのバッファ長 (終端を含む) */
			static constexpr size_t WIDE_PATH_BUFFER_COUNT = 512;


			/** 小文字化した拡張子 ('.' 込み) を返す。拡張子が無ければ空文字列 */
			std::string GetLowerExtension(const std::string& path)
			{
				const size_t dotPos = path.find_last_of('.');
				if (dotPos == std::string::npos) {
					return std::string();
				}
				std::string extension = path.substr(dotPos);
				for (char& c : extension) {
					c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
				}
				return extension;
			}


			/**
			 * DirectXTex のファイル API はワイド文字パスを取るため変換する。
			 * mbstowcs は失敗時に (size_t)-1 を返し、切り詰め時は終端を書かないため、
			 * どちらもここで担保する (切り詰めは失敗扱い)。
			 * @param path     変換元のマルチバイトパス
			 * @param outPath  変換先バッファ
			 * @param outCount 変換先バッファの要素数
			 * @return 変換に成功したら true
			 */
			bool ToWidePath(const std::string& path, wchar_t* outPath, const size_t outCount)
			{
				const size_t converted = std::mbstowcs(outPath, path.c_str(), outCount - 1);
				if (converted == static_cast<size_t>(-1) || converted >= outCount - 1) {
					return false;
				}
				outPath[converted] = L'\0';
				return true;
			}


#if defined(AQ_PLATFORM_MAC)
			/**
			 * stb_image で PNG / JPG 等をデコードし、RGBA8 の ScratchImage に詰め直す。
			 * WIC が RGBA8 を返すケースに合わせて 4 チャンネル固定で受け取る。
			 */
			bool LoadWithStbImage(const std::string& path, DirectX::TexMetadata* outMetadata, DirectX::ScratchImage& outImage)
			{
				int width    = 0;
				int height   = 0;
				int channels = 0;
				stbi_uc* pixels = stbi_load(path.c_str(), &width, &height, &channels, 4);
				if (!pixels) {
					return false;
				}

				DirectX::Image image = {};
				image.width      = static_cast<size_t>(width);
				image.height     = static_cast<size_t>(height);
				image.format     = DXGI_FORMAT_R8G8B8A8_UNORM;
				image.rowPitch   = static_cast<size_t>(width) * 4;
				image.slicePitch = image.rowPitch * static_cast<size_t>(height);
				image.pixels     = pixels;

				// InitializeFromImage はピクセルを複製するため、直後に stb 側を解放してよい。
				const HRESULT hr = outImage.InitializeFromImage(image);
				stbi_image_free(pixels);
				if (FAILED(hr)) {
					return false;
				}

				if (outMetadata) {
					*outMetadata = outImage.GetMetadata();
				}
				return true;
			}
#endif // AQ_PLATFORM_MAC
		}


		bool LoadImageFile(const std::string& path, DirectX::TexMetadata* outMetadata, DirectX::ScratchImage& outImage)
		{
			if (path.empty()) {
				return false;
			}

			wchar_t widePath[WIDE_PATH_BUFFER_COUNT] = {};
			if (!ToWidePath(path, widePath, WIDE_PATH_BUFFER_COUNT)) {
				return false;
			}

			// DDS は tkm マテリアルで多用。TGA は WIC 非対応のため専用ローダ。
			// どちらもプラットフォーム非依存の DirectXTex 実装で読める。
			const std::string extension = GetLowerExtension(path);
			if (extension == ".dds") {
				return SUCCEEDED(DirectX::LoadFromDDSFile(widePath, DirectX::DDS_FLAGS_NONE, outMetadata, outImage));
			}
			if (extension == ".tga") {
				return SUCCEEDED(DirectX::LoadFromTGAFile(widePath, DirectX::TGA_FLAGS_NONE, outMetadata, outImage));
			}

			// それ以外 (.png/.jpg 等) は WIC。Mac には WIC が無いので stb_image を使う。
#if defined(AQ_PLATFORM_WINDOWS_FAMILY)
			return SUCCEEDED(DirectX::LoadFromWICFile(widePath, DirectX::WIC_FLAGS_NONE, outMetadata, outImage));
#elif defined(AQ_PLATFORM_MAC)
			return LoadWithStbImage(path, outMetadata, outImage);
#else
			return false;
#endif
		}
	}
}
