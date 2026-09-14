// CopyToBackBuffer のフルスクリーン変換描画に使う MSL。
//
// **なぜ .fx ではないのか**
// この描画は「バックバッファのフォーマットが表示 RT と違うときに変換して出す」という
// **Metal バックエンド内部の都合**で、エンジンのシェーダ資産ではない。.fx を増やすと
//  (a) 設計の「.fx は無改変で移植する」が崩れ、
//  (b) shader_entries.txt に載せる必要が出て Vulkan / D3D 側のビルドにも波及し、
//  (c) 起動時のシェーダコンパイル本数(設計書 §13-2 で問題視している)がさらに増える。
// よって .fx ではなく、**手書きの MSL を直接置く**。shader_entries.txt には載せないこと。
//
// **なぜ .mm への埋め込みをやめてこのファイルにしたのか**(iOS 移植 P5b)
// iOS 実機では newLibraryWithSource: が SIGBUS で即死するため、この MSL も
// ビルド時に .metallib へ焼く必要がある(設計書/iOS移植設計.md §4.6)。
// コンパイラへ渡せる実ファイルが要るので独立した .metal にした。
//   iOS : Tools/ShaderCompile/compile_msl.cmake が
//         Assets/Shader/msl-ios/<sdk>/FullscreenBlit.metallib を生成し、実行時は
//         newLibraryWithURL: で読む。
//   Mac : このファイルをそのまま読んで newLibraryWithSource: でコンパイルする
//         (macOS では実行時コンパイルが動いており、変える理由が無い)。
// **どちらも同じこのファイルを見る**ので、二重管理にはならない。
// 読み出し側は aqEngine/Graphics/Metal/MetalGraphicsDeviceImpl.mm の
// EnsureFullscreenBlitPipeline()。関数名(aqFullscreenBlitVS / aqFullscreenBlitPS)と
// ファイル名は向こうと**一対一で対応している。片方だけ変えないこと。**
//
// **頂点バッファは使わない**。vertex_id からクリップ空間を覆う大三角形
// (-1,-1)-(3,-1)-(-1,3) を作る。全画面を 2 枚の三角形で覆うより
// 対角線上の重複シェーディングが無い分だけ速く、頂点記述子も要らない。
//
// **Y 反転について**(ここを間違えると上下逆さまになる)
//  - Metal のクリップ空間は D3D と同じく **y = +1 が画面の上端**
//    (OpenGL/Vulkan のような下端ではない)。
//  - Metal のテクスチャ座標は **v = 0 がテクスチャの先頭行(上端)**。
//  - よって「画面の上端 (y=+1)」に「テクスチャの上端 (v=0)」を貼るには
//    v = (1 - y) * 0.5 とする。u はそのまま u = (x + 1) * 0.5。
//  - src テクスチャはエンジンが同じ Metal のラスタライズ規約で描いたものなので、
//    blit 経路(生バイトコピー)と同じ向きで出る。**追加の反転は不要**。
#include <metal_stdlib>
using namespace metal;

struct FullscreenVSOut
{
	float4 position [[position]];
	float2 uv;
};

vertex FullscreenVSOut aqFullscreenBlitVS(uint vertexId [[vertex_id]])
{
	// クリップ空間を覆う大三角形。頂点バッファは要らない。
	const float2 positions[3] = { float2(-1.0, -1.0), float2(3.0, -1.0), float2(-1.0, 3.0) };

	const float2 p = positions[vertexId];
	FullscreenVSOut out;
	out.position = float4(p, 0.0, 1.0);

	// クリップ空間は y = +1 が上端、テクスチャは v = 0 が上端。よって v は反転して取る。
	out.uv = float2((p.x + 1.0) * 0.5, (1.0 - p.y) * 0.5);
	return out;
}

fragment float4 aqFullscreenBlitPS(FullscreenVSOut in [[stage_in]],
                                   texture2d<float> src [[texture(0)]],
                                   sampler          samp [[sampler(0)]])
{
	// 変換はフォーマット間の読み替えだけ。トーンマップは既にポストプロセスが済ませている。
	return src.sample(samp, in.uv);
}
