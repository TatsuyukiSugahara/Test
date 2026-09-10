//==============================================================================
//  sal.h ― 非 Windows 向け SAL 注釈の空マクロ定義(本リポジトリ独自の互換ヘッダ)
//
//  同梱 DirectXMath の `DirectXMath.h` が無条件に `#include "sal.h"` している。
//  Windows では Windows SDK が提供するが、macOS には無く、上流の DirectXMath /
//  DirectX-Headers のどちらも sal.h を同梱していないため、こちらで用意する。
//  中身は静的解析用の注釈なので、非 Windows では全て空に潰してよい。
//
//  定義するのは同梱 DirectXMath と DirectXTex が実際に使う注釈だけ。SAL の全体を
//  移植する意図は無く、取りこぼしが出たら「使われているものだけ」を足す方針とする。
//  多くは `DirectX-Headers/include/wsl/stubs/basetsd.h` も定義しているが、そちらも
//  空定義でトークン列が一致するため、どちらが先に読まれても再定義エラーにならない
//  (`#ifndef` を付けているのは、将来 basetsd.h 側の定義が空でなくなったときに
//  こちらが上書きしないようにするため)。
//
//  このディレクトリは `CMakeLists.txt` で **非 Windows のときだけ** インクルード
//  パスに載せる。Windows で載せると SDK の sal.h を隠してしまう。
//==============================================================================
#pragma once

#ifdef _WIN32
	#error "ThirdParty/WinCompat/sal.h は非 Windows 専用です。Windows では Windows SDK の sal.h を使ってください"
#endif


// ----------------------------------------------------------------------------
//  入力
// ----------------------------------------------------------------------------
#ifndef _In_
	#define _In_
#endif
#ifndef _In_z_
	#define _In_z_
#endif
#ifndef _In_opt_
	#define _In_opt_
#endif
#ifndef _In_count_
	#define _In_count_(x)
#endif
#ifndef _In_range_
	#define _In_range_(x, y)
#endif
#ifndef _In_reads_
	#define _In_reads_(x)
#endif
#ifndef _In_reads_opt_
	#define _In_reads_opt_(x)
#endif
#ifndef _In_reads_bytes_
	#define _In_reads_bytes_(x)
#endif


// ----------------------------------------------------------------------------
//  出力
// ----------------------------------------------------------------------------
#ifndef _Out_
	#define _Out_
#endif
#ifndef _Out_opt_
	#define _Out_opt_
#endif
#ifndef _Out_writes_
	#define _Out_writes_(x)
#endif
#ifndef _Out_writes_opt_
	#define _Out_writes_opt_(x)
#endif
#ifndef _Out_writes_all_
	#define _Out_writes_all_(x)
#endif
#ifndef _Out_writes_bytes_
	#define _Out_writes_bytes_(x)
#endif
#ifndef _Out_writes_bytes_to_opt_
	#define _Out_writes_bytes_to_opt_(x, y)
#endif
#ifndef _Outptr_
	#define _Outptr_
#endif
#ifndef _COM_Outptr_
	#define _COM_Outptr_
#endif


// ----------------------------------------------------------------------------
//  入出力
// ----------------------------------------------------------------------------
#ifndef _Inout_
	#define _Inout_
#endif
#ifndef _Inout_opt_
	#define _Inout_opt_
#endif
#ifndef _Inout_updates_bytes_
	#define _Inout_updates_bytes_(x)
#endif
#ifndef _Inout_updates_all_
	#define _Inout_updates_all_(x)
#endif
#ifndef _Inout_updates_all_opt_
	#define _Inout_updates_all_opt_(x)
#endif


// ----------------------------------------------------------------------------
//  その他
//
//  `_When_` は第 2 引数に別の注釈を取る(例: `_When_(a == b, _In_)`)。空に潰す
//  ときは引数ごと消えるので、入れ子の注釈も一緒に消える。
// ----------------------------------------------------------------------------
#ifndef _When_
	#define _When_(x, y)
#endif
#ifndef _Success_
	#define _Success_(x)
#endif
#ifndef _Analysis_assume_
	#define _Analysis_assume_(x)
#endif
#ifndef _Use_decl_annotations_
	#define _Use_decl_annotations_
#endif
