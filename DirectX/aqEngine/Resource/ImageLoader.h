#pragma once
#include <string>


namespace aq
{
	namespace res
	{
		/**
		 * 画像ファイルを DirectXTex の ScratchImage へ読み込む。
		 * 拡張子で DDS / TGA / それ以外 (PNG・JPG 等) を振り分け、
		 * それ以外の経路だけがプラットフォームで変わる (Windows = WIC / Mac = stb_image)。
		 * ワイド文字パスへの変換も内部で吸収するため、呼び出し側は UTF-8 の std::string を渡すだけでよい。
		 * @param path        画像ファイルのパス (マルチバイト)
		 * @param outMetadata メタデータの受け取り先。不要なら nullptr
		 * @param outImage    デコード結果の受け取り先
		 * @return 読み込みに成功したら true
		 */
		bool LoadImageFile(const std::string& path, DirectX::TexMetadata* outMetadata, DirectX::ScratchImage& outImage);
	}
}
