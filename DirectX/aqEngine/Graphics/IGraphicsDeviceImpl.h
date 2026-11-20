#pragma once
#include <cstdint>
#include <memory>
#include <vector>
#include "IBuffer.h"
#include "IShader.h"
#include "IShaderResourceView.h"
#include "ISamplerState.h"
#include "IDepthMap.h"
#include "IGpuBuffer.h"
#include "GraphicsTypes.h"

struct ImDrawData;   // 前方宣言のみ。imgui.h を引き込まない
struct ImGuiContext;


namespace aq
{
	namespace graphics
	{
		class RenderContext;
		class IRenderTarget;

		/**
		 * Graphics API Implementor interface (Bridge Pattern)
		 *
		 * D3D11 / D3D12 / Vulkan など各 API の実装クラスが継承する。
		 * Engine や GraphicsDevice はこのインターフェース越しにのみ API と話す。
		 */
		class IGraphicsDeviceImpl
		{
		public:
			virtual ~IGraphicsDeviceImpl() = default;

			/** デバイス・スワップチェーン等の初期化 */
			virtual bool Initialize(NativeWindowHandle window, uint32_t width, uint32_t height) = 0;

			/** 解放 */
			virtual void Finalize() = 0;

			/**
			 * PLM(UWP)サスペンド/レジューム。
			 * OnSuspend: GPU をアイドルにし、可能なら未使用メモリを解放する
			 *            (復帰後の描画破壊/TDR を防ぐ)。既定 no-op。
			 */
			virtual void OnSuspend() {}
			virtual void OnResume()  {}

			/**
			 * 描画対象のウィンドウが差し替わったときに、サーフェスとスワップチェーンを作り直す。
			 *
			 * 作り直すのは サーフェス / スワップチェーン / その画像ビュー / present セマフォ に限る。
			 * デバイス・パイプライン・オフスクリーン RT・テクスチャは保持する
			 * (デバイスごと作り直すと復帰が重く、Android でもデバイスロスト自体は稀なため)。
			 * 窓を手放さないプラットフォーム(Win32 / UWP / Mac)では起こらないので既定は no-op。
			 *
			 * @param window 新しいネイティブウィンドウ
			 * @return 以後描画してよいなら true。false の間はフレームを飛ばして再試行してよい
			 */
			virtual bool RecreateSurface(NativeWindowHandle /*window*/) { return true; }

			/**
			 * 実際に提示している面のサイズ(ウィンドウ座標系)。
			 * 回転やリサイズで初期化時の値とずれるため、ImGui の DisplaySize のように
			 * スクリーン座標を扱う側はこちらを正とする。
			 * 取得できないバックエンドは false を返し、呼び出し側は従来値を使い続ける。
			 */
			virtual bool GetSurfaceSize(uint32_t& /*outWidth*/, uint32_t& /*outHeight*/) const { return false; }

			/**
			 * 提出済みの全 GPU 作業が完了するまで待つ(GPU アイドル化)。
			 * GPU が参照中かもしれないリソースを破棄する前(終了処理など)に呼ぶ。
			 * 即時実行モデル(D3D11)では不要のため既定は no-op。
			 */
			virtual void WaitIdle() {}

			/**
			 * ImGui のバックエンド接続(既定 no-op。AQ_IMGUI 構成でのみ意味を持つ)。
			 * ImGuiInit / ImGuiNewFrame / ImGuiShutdown はメインスレッドで呼ぶ。
			 * ImGuiShutdown は GPU アイドル(WaitIdle)の後に呼ぶこと。
			 *
			 * @return ImGuiInit は初期化に失敗したら false
			 */
			virtual bool ImGuiInit()     { return true; }
			virtual void ImGuiShutdown() {}
			virtual void ImGuiNewFrame() {}

			/**
			 * ImGui の描画データを描く(ImGuiRenderCommand::Execute から、レンダースレッドで呼ばれる)。
			 * Vulkan / Metal は描画データを保持し、CopyToBackBuffer 後に swapchain / drawable へ描く。
			 */
			virtual void ImGuiRender(ImDrawData* /*drawData*/) {}

			/**
			 * エディタ用 ImGui コンテキストのバックエンド接続(AQ_EDITOR 構成のみ使う。既定 no-op)。
			 * ImGuiInit(ゲーム用コンテキスト)の後に、メインスレッドで呼ぶ。
			 * 両コンテキストを控え、レンダースレッドで描くときにどちらかを current にする。
			 * ImGuiShutdownEditor は ImGuiShutdown より前に呼ぶこと。
			 *
			 * @return ImGuiInitEditor は未対応のバックエンドか初期化に失敗したら false
			 */
			virtual bool ImGuiInitEditor(ImGuiContext* /*gameCtx*/, ImGuiContext* /*editorCtx*/) { return false; }
			virtual void ImGuiShutdownEditor() {}
			virtual void ImGuiNewFrameEditor() {}

			/**
			 * エディタ用コンテキストの描画データをバックバッファへ描く(レンダースレッドで呼ばれる)。
			 * バックバッファをクリアしてから描くので、CopyToBackBuffer の代わりに呼ぶ。既定 no-op。
			 */
			virtual void RenderEditorOverlay(ImDrawData* /*editorDrawData*/) {}

			/**
			 * RT を ImGui::Image に渡せる ID に変換する(アルファを 1 に固定して読む)。
			 * 未対応のバックエンドは nullptr(既定)。
			 */
			virtual void* GetImGuiTextureId(IRenderTarget* /*rt*/) { return nullptr; }

			/** VSync を実行時に切り替えられるか(現状 D3D12 のみ) */
			virtual bool CanToggleVSync() const { return false; }
			virtual bool GetVSync() const       { return true; }
			virtual void SetVSync(bool /*enabled*/) {}

			/** デバッグ表示用のバックエンド名("D3D11" / "D3D12" / "Vulkan" / "Metal") */
			virtual const char* GetBackendName() const { return "?"; }

			/**
			 * 描画解像度の実行時変更に対応しているか(設計書/エンジン課題解消設計.md P4)。
			 * false(既定)のバックエンドは従来どおり**固定寸法のまま**動く(Engine は検出しても何もしない)。
			 * true を返すバックエンドは ResizeMainTargets と ReleaseOffscreenRenderTarget を必ず実装すること。
			 */
			virtual bool SupportsRenderResize() const { return false; }

			/**
			 * スワップチェーン(提示面)とメイン RT / 深度を reqW × reqH で作り直す。
			 * 呼べるのは WaitIdle 済みの区間(在フライトのコマンドが無い)だけで、ゲームスレッドから呼ぶ。
			 * メイン RT のハンドル(添字)は変わらない。
			 *
			 * @param outW / outH 実際に確定した寸法(要求値と異なり得る。Vulkan は currentExtent を優先する)。
			 *                    呼び出し側はこの寸法で全パスの RT を作る。
			 * @return 成功したら true。失敗したら false(旧状態へ戻れるとは限らない。呼び出し側は再試行する)
			 */
			virtual bool ResizeMainTargets(uint32_t /*reqW*/, uint32_t /*reqH*/, uint32_t& /*outW*/, uint32_t& /*outH*/)
			{
				return false;
			}

			/**
			 * 提示面(スワップチェーン)とレンダー解像度(メイン RT)を別々に作り直せるか
			 * (設計書/エディタ全体設計.md 5.9 節 3)。
			 * false(既定)のバックエンドでは、Engine は従来どおり 1 つの寸法で ResizeMainTargets を呼ぶ。
			 * true を返すバックエンドは ResizePresentSurface と ResizeRenderTargets を必ず実装すること。
			 */
			virtual bool SupportsSeparatePresentSize() const { return false; }

			/**
			 * 提示面(スワップチェーン)だけを w × h で作り直す。メイン RT とパスには関係しない。
			 * 呼べるのは WaitIdle 済みの区間だけで、ゲームスレッドから呼ぶ。
			 * @return 成功したら true。失敗したら false(呼び出し側は再試行する)
			 */
			virtual bool ResizePresentSurface(uint32_t /*w*/, uint32_t /*h*/) { return false; }

			/**
			 * メイン RT / 深度だけを reqW × reqH で作り直す。提示面は触らない。
			 * 呼べる区間とハンドル・確定寸法の扱いは ResizeMainTargets と同じ。
			 * @return 成功したら true。失敗したら false(旧状態へ戻れるとは限らない。呼び出し側は再試行する)
			 */
			virtual bool ResizeRenderTargets(uint32_t /*reqW*/, uint32_t /*reqH*/, uint32_t& /*outW*/, uint32_t& /*outH*/)
			{
				return false;
			}

			/**
			 * オフスクリーン RT を 1 本解放する。ハンドル(添字)は空き番号として再利用される。
			 * WaitIdle 済みの区間で呼ぶこと。解放済みの添字を GetRenderTarget に渡すと nullptr を返す。
			 */
			virtual void ReleaseOffscreenRenderTarget(uint32_t /*index*/) {}

			/**
			 * RenderContext に API 依存のコンテキストを渡す。
			 * D3D11 なら ID3D11DeviceContext*、D3D12 なら CommandList など。
			 */
			virtual void SetupRenderContext(RenderContext& outContext) = 0;

			/** メインRTの数を返す。RenderTargetHandle の有効インデックス範囲は [0, count) */
			virtual uint32_t GetMainRenderTargetCount() const = 0;

			/** メインのレンダリングターゲットを返す */
			virtual IRenderTarget& GetMainRenderTarget(uint32_t index) = 0;

			/**
			 * ハンドルが示す RT を返す。メイン・オフスクリーン両方に対応。
			 * 無効なインデックスの場合は nullptr を返す。
			 */
			virtual IRenderTarget* GetRenderTarget(uint32_t index) = 0;

			/**
			 * オフスクリーン RT を生成してそのインデックスを返す。
			 * 失敗時は ~0u (INVALID) を返す。
			 */
			virtual uint32_t CreateOffscreenRenderTarget(uint32_t width, uint32_t height) = 0;
			virtual uint32_t CreateOffscreenRenderTarget(const RenderTargetDesc& desc) = 0;

			/** 描画結果を画面に出す */
			virtual void Present() = 0;

			/** メインRTを現在のバックバッファへコピー（不要な API では空実装） */
			virtual void CopyToBackBuffer(IRenderTarget& src) = 0;

			/** デフォルトのラスタライザーなど API 固有の初期レンダーステートを設定する */
			virtual void SetupDefaultRenderState(RenderContext& context) = 0;

			/**
			 * リソースファクトリー
			 * API 非依存のインターフェースを返す。呼び出し元は実装型を知る必要がない。
			 */
			virtual std::unique_ptr<IVertexBuffer>       CreateVertexBuffer(uint32_t vertexNum, uint32_t stride, const void* data) = 0;
			virtual std::unique_ptr<IVertexBuffer>       CreateDynamicVertexBuffer(uint32_t vertexNum, uint32_t stride, const void* data) = 0;
			virtual std::unique_ptr<IIndexBuffer>        CreateIndexBuffer(uint32_t indexNum, const void* data) = 0;
			virtual std::unique_ptr<IIndexBuffer>        CreateDynamicIndexBuffer(uint32_t indexNum, IndexFormat format, const void* data) = 0;
			virtual std::unique_ptr<IConstantBuffer>     CreateConstantBuffer(const void* data, uint32_t size) = 0;
			virtual std::unique_ptr<IShader>             CreateShader(const char* filePath, const char* entryFunc, IShader::ShaderType type) = 0;
			virtual std::unique_ptr<ISamplerState>       CreateSamplerState(const SamplerDesc& desc) = 0;
			virtual std::unique_ptr<IShaderResourceView> CreateTexture2D(const Texture2DDesc& desc, const ImageData& data) = 0;
			virtual std::unique_ptr<IDepthMap>           CreateDepthMap(uint32_t width, uint32_t height) = 0;

			/**
			 * テクスチャアップロードのバッチ制御。
			 * Begin/End で挟むと、その間の CreateTexture2D のコピー実行/GPU 待機を End で 1 回に
			 * まとめる。per-texture の同期 GPU 待ちによるロードヒッチ (複数枚仕上げ時の N×WaitForGPU) を抑える。
			 * 未対応バックエンドは no-op (CreateTexture2D は従来どおり即時同期アップロード)。
			 * ネスト不可。End までに作られたテクスチャは End 後に使用可能になる。
			 */
			virtual void BeginBatchedTextureUploads() {}
			virtual void EndBatchedTextureUploads()   {}

			/**
			 * R32_Float オフスクリーン RT (rtIndex) の内容を CPU へ取得する (GPU→CPU リードバック)。
			 * frames-in-flight を考慮した遅延リードバック: 直近で GPU 完了済みのデータがあれば
			 * outData (width*height 個の float) に格納して true を返し、同時に今フレームのコピーを記録する。
			 * 未完了・未対応バックエンドでは false (呼び出し側は前回値を保持 = 保守的に全可視)。
             * レンダースレッドのコマンド記録中に呼ぶこと。
			 */
			virtual bool ReadbackOffscreenR32(uint32_t /*rtIndex*/, uint32_t /*width*/, uint32_t /*height*/,
			                                  std::vector<float>& /*outData*/) { return false; }

			/**
			 * デバッグ: リードバック診断。
			 * copies=コピー記録回数 / maps=Map成功回数 / createFails=バッファ生成失敗回数 /
			 * stamps=Presentでフェンス刻印した回数 / validCount=現在有効なスロット数 /
			 * fenceReady=完了フェンスが刻印値に追いついているか(1/0)。
			 */
			virtual void GetReadbackDebug(uint32_t& copies, uint32_t& maps, uint32_t& createFails,
			                              uint32_t& stamps, uint32_t& validCount, uint32_t& fenceReady)
			{
				copies = 0; maps = 0; createFails = 0; stamps = 0; validCount = 0; fenceReady = 0;
			}

			// ── GPU 駆動用バッファ (クラスタカリング)。未対応バックエンドは nullptr。 ──
			/** 構造化バッファ (SRV 読み取り専用, 初期データあり)。 */
			virtual std::unique_ptr<IGpuBuffer> CreateStructuredBuffer(uint32_t /*stride*/, uint32_t /*count*/, const void* /*data*/) { return nullptr; }
			/** RAW(ByteAddress) バッファ。srv/uav を指定。uav=true は GPU 書き込み (DEFAULT ヒープ)。 */
			virtual std::unique_ptr<IGpuBuffer> CreateRawBuffer(uint32_t /*byteSize*/, bool /*srv*/, bool /*uav*/, const void* /*initData*/) { return nullptr; }
		};
	}
}
