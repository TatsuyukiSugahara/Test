#include "aq.h"
#include "ImageLoader.h"

#include <cctype>
#include <cstdlib>

#if !defined(AQ_PLATFORM_WINDOWS_FAMILY)
// stb_image の実体はこの TU だけで生成する。
// Windows は WIC を使うため実体化しない (未使用コードと警告を持ち込まないため)。
// 非 Windows (Mac / Android) は WIC が無いのでこちらを使う。
#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"
#endif // !AQ_PLATFORM_WINDOWS_FAMILY


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


#if !defined(AQ_PLATFORM_WINDOWS_FAMILY)
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
#endif // !AQ_PLATFORM_WINDOWS_FAMILY


			/**
			 * BC 圧縮テクスチャを、GPU が BC に対応していない環境でだけ RGBA8 へ展開する。
			 * (設計書/iOS移植設計.md §4.3 案 a)
			 *
			 * iOS シミュレータのように supportsBCTextureCompression == NO の環境では、
			 * BC のピクセルフォーマットでテクスチャを作れずアセットが丸ごと落ちる。
			 * DirectXTex の BC ソフトコーデックは非 Windows ビルドにも同梱されているので、
			 * ここで RGBA8 へ展開して渡す(アセットは無改変。VRAM は増える)。
			 *
			 * 対応環境・非圧縮フォーマットでは何もせず true を返す。既定は「対応あり」なので、
			 * Windows / Mac / Android はこの関数を素通りする。
			 * @param path         診断ログ用の元パス
			 * @param outMetadata  展開したときは展開後のメタデータへ差し替える (nullptr 可)
			 * @param outImage     展開対象。展開したときは中身が入れ替わる
			 * @return 展開しなかった場合と展開に成功した場合は true、展開に失敗したら false
			 */
			bool DecompressIfBlockCompressionUnsupported(const std::string& path, DirectX::TexMetadata* outMetadata, DirectX::ScratchImage& outImage)
			{
				const DirectX::TexMetadata& metadata = outImage.GetMetadata();
				if (!DirectX::IsCompressed(metadata.format)) {
					return true;
				}
				if (aq::graphics::IsBlockCompressionSupported()) {
					return true;
				}

				// **ScratchImage 全体を渡す overload を使うこと。** 1 枚だけを取る
				// overload (Decompress(const Image&, ...)) だと、キューブマップ
				// (Assets/Sky/SkyCube.dds) の残り 5 面とミップが落ちる。
				DirectX::ScratchImage decompressed;
				const HRESULT hr = DirectX::Decompress(
					outImage.GetImages(), outImage.GetImageCount(), metadata,
					DXGI_FORMAT_R8G8B8A8_UNORM, decompressed);
				if (FAILED(hr)) {
					// 握り潰すとテクスチャ無しで進んでしまい原因が追えない
					// (WIC / stb_image の失敗時と同じ流儀で必ず残す)。
					aq::StartupMarkf("[img] BC decompress failed hr=0x%08X path=%s",
						static_cast<unsigned int>(hr), path.c_str());
					return false;
				}

				outImage = std::move(decompressed);
				if (outMetadata) {
					// format が BC から RGBA8 へ変わるため、メタデータも差し替える。
					*outMetadata = outImage.GetMetadata();
				}

				// 展開が走るのは起動時の数枚だけ(対象 DDS は 7 枚)。コストの当たりを
				// 付けられるよう 1 行だけ残す。毎フレーム出るものではない。
				aq::StartupMarkf("[img] BC decompressed %u image(s) -> RGBA8 path=%s",
					static_cast<unsigned int>(outImage.GetImageCount()), path.c_str());
				return true;
			}
		}


		bool LoadImageFile(const std::string& path, DirectX::TexMetadata* outMetadata, DirectX::ScratchImage& outImage)
		{
			if (path.empty()) {
				return false;
			}

			// DirectXTex はパスを OS へ丸投げするため、ここで実在パスへ解決しておく。
			// CWD 相対のままだと UWP で破綻する(CWD = パッケージの読み取り専用ルートで、
			// ゲームアセットは <package>/Game/Assets/... に入る)。
			const std::string resolved = ResolveExistingResourcePath(path);

			wchar_t widePath[WIDE_PATH_BUFFER_COUNT] = {};
			if (!ToWidePath(resolved, widePath, WIDE_PATH_BUFFER_COUNT)) {
				return false;
			}

			// DDS は tkm マテリアルで多用。TGA は WIC 非対応のため専用ローダ。
			// どちらもプラットフォーム非依存の DirectXTex 実装で読める。
			const std::string extension = GetLowerExtension(path);
			bool loaded = false;
			if (extension == ".dds") {
				loaded = SUCCEEDED(DirectX::LoadFromDDSFile(widePath, DirectX::DDS_FLAGS_NONE, outMetadata, outImage));
			} else if (extension == ".tga") {
				loaded = SUCCEEDED(DirectX::LoadFromTGAFile(widePath, DirectX::TGA_FLAGS_NONE, outMetadata, outImage));
			} else {
				// それ以外 (.png/.jpg 等) は WIC。Mac / Android には WIC が無いので stb_image を使う。
#if defined(AQ_PLATFORM_WINDOWS_FAMILY)
				const HRESULT hr = DirectX::LoadFromWICFile(widePath, DirectX::WIC_FLAGS_NONE, outMetadata, outImage);
				if (FAILED(hr)) {
					// 失敗は黙って握り潰すとテクスチャ無しで進んでしまい原因が分からなくなる
					// (UWP はデバッガを繋げないので特に)。hr とパスを必ず残す。
					aq::StartupMarkf("[img] WIC load failed hr=0x%08X path=%s",
						static_cast<unsigned int>(hr), path.c_str());
				}
				loaded = SUCCEEDED(hr);
#else
				// 解決済みパスを渡すこと。元の CWD 相対パスを渡すと、CWD をアプリが
				// 決められないプラットフォーム(Android は "/")で開けない。
				// Mac では .app が CWD を Game/ へ移しているため元のパスでも通っていた。
				loaded = LoadWithStbImage(resolved, outMetadata, outImage);
				if (!loaded) {
					// WIC 側と同じ理由で失敗を必ず残す。テクスチャ無しで進むと
					// 「文字が塊になる」「絵が出ない」だけが症状として出て原因が追えない。
					aq::StartupMarkf("[img] stb_image load failed path=%s", resolved.c_str());
				}
#endif
			}

			if (!loaded) {
				return false;
			}

			// ここが DDS / TGA / PNG すべての単一の入口なので、BC 展開の判定も
			// ここ 1 箇所で済ませる(設計書/iOS移植設計.md §4.3)。
			return DecompressIfBlockCompressionUnsupported(path, outMetadata, outImage);
		}
	}
}
