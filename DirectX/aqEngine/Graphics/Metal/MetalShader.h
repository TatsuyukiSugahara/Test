#pragma once
// Metal のシェーダ(設計書/MetalBackend設計.md §9)。
//
// **本ヘッダは Objective-C++ 専用**で、Graphics/Metal/ 配下の .mm からのみ include する。
#if defined(ENGINE_GRAPHICS_METAL)
#include "Graphics/Metal/MetalCommon.h"
#include "Graphics/IShader.h"
#include <string>


namespace aq
{
	namespace graphics
	{
		/**
		 * Assets 相対のシェーダパスを実ファイルのパスへ解決する
		 *
		 * 実体は MetalShader.mm(VulkanShader.cpp / D3D12Shader.cpp と同じ規則)。
		 * **同じ規則を 2 箇所に書かない**ために公開している。現在の利用者は
		 * MetalGraphicsDeviceImpl.mm の EnsureFullscreenBlitPipeline()
		 * (フルスクリーン blit の MSL / .metallib の在り処を求める)。
		 * @param filePath "Assets/Shader/..." などのプロジェクト相対パス(絶対パスならそのまま)
		 * @return 解決後のパス。見つからなければ入力をそのまま返す
		 */
		std::string ResolveShaderFilePath(const char* filePath);




		/**
		 * Metal シェーダ
		 *
		 * Tools/ShaderCompile/compile_msl.cmake がビルド時に生成したものを読み、
		 * newFunctionWithName: で MTLFunction を取り出す(設計書 §9.2)。
		 * **読むものはプラットフォームで違う**(設計書/iOS移植設計.md §4.6):
		 *   macOS … .metal(MSL)を newLibraryWithSource: で実行時コンパイルする。
		 *   iOS   … ビルド時に焼いた .metallib を newLibraryWithURL: で読む。
		 *           実機では newLibraryWithSource: が SIGBUS で即死するため必須。
		 *
		 * 探索パスは
		 *   macOS: <.fx の親>/msl/<stem>.<entry>.<vs|ps|cs>.metal
		 *   iOS  : <.fx の親>/msl-ios/<sdk>/<stem>.<entry>.<vs|ps|cs>.metallib
		 * で、compile_msl.cmake の出力名と**一対一で対応している**(片方だけ変えないこと)。
		 * ディレクトリ名は metal::MSL_DIR_NAME / metal::METALLIB_SDK_DIR_NAME。
		 *
		 * 参照カウントは MRR(ARC ではない)。library_ / function_ は newXxx 系で
		 * +1 されたものを持つので、Release() で対に release する。
		 */
		class MetalShader : public IShader
		{
		private:
			/** Metal オブジェクト */
			id<MTLDevice>   device_;
			id<MTLLibrary>  library_;
			id<MTLFunction> function_;

			/** 頂点入力レイアウト(VS のみ。持たない VS では nil / 0) */
			MTLVertexDescriptor* vertexDescriptor_;
			uint32_t             vertexStride_;
			uint32_t             instanceStride_;

			/** ロード情報 */
			std::string filePath_;
			std::string entryFuncName_;
			ShaderType  type_;


		private:
			/** .metal の読み込みと実行時コンパイルの本体(計測は Load 側で行う) */
			bool LoadMsl();

			/** 隣の .spv を spirv_reflect で読み、MTLVertexDescriptor を組む(設計書 §9.3) */
			void BuildVertexDescriptor();


		public:
			explicit MetalShader(id<MTLDevice> device);
			~MetalShader() override;


		public:
			bool Load(const char* filePath, const char* entryFuncName, ShaderType shaderType) override;
			void Release() override;

			/**
			 * Metal はバイトコードを外へ渡す使い方をしない(PSO 生成は MTLFunction 経由)ので
			 * 常に nullptr / 0 を返す。D3D の「バイトコードを入力レイアウト生成へ渡す」経路も
			 * 持たない(頂点入力は隣の .spv をリフレクションして組む。設計書 §9.3)。
			 */
			inline void*  GetByteCode() const override     { return nullptr; }
			inline size_t GetByteCodeSize() const override { return 0; }


			/**
			 * PSO 生成用
			 */
		public:
			/** PSO へ渡す MTLFunction。Load に失敗していれば nil */
			inline id<MTLFunction> GetFunction() const { return function_; }

			inline ShaderType  GetType() const          { return type_; }
			inline const char* GetFilePath() const      { return filePath_.c_str(); }
			inline const char* GetEntryFuncName() const { return entryFuncName_.c_str(); }


			/**
			 * 頂点入力レイアウト(設計書 §9.3)
			 *
			 * ビルド時に .metal と同じ場所へ残した .spv を spirv_reflect で読んで組む。
			 * **VS 以外と、頂点入力を持たない VS(フルスクリーンパス)では nil / 0** になる。
			 * nil は正常系なので、PSO 生成側は分岐して扱うこと。
			 */
		public:
			/** PSO へ渡す MTLVertexDescriptor。VS 以外・頂点入力なしなら nil */
			inline MTLVertexDescriptor* GetVertexDescriptor() const { return vertexDescriptor_; }

			/** per-vertex ストリーム(buffer 30)の stride。リフレクションによるパック済み前提の値 */
			inline uint32_t GetVertexStride() const { return vertexStride_; }

			/** per-instance ストリーム(buffer 29)の stride。インスタンス属性が無ければ 0 */
			inline uint32_t GetInstanceStride() const { return instanceStride_; }


			/**
			 * 起動時間の計測(設計書 §12 P0.5 / §13-2)
			 */
		public:
			/**
			 * ここまでの Load の本数と合計所要時間を 1 行ログへ出し、集計を捨てる。
			 *
			 * Load は 59 本呼ばれるので 1 本 1 行では読めない。しきい値超えだけ
			 * その場で出し、合計はこの関数でまとめて出す。
			 * 誰も呼ばなかった場合はプロセス終了時に自動で 1 回だけ出る。
			 */
			static void LogLoadSummary();
		};
	}
}
#endif // ENGINE_GRAPHICS_METAL
