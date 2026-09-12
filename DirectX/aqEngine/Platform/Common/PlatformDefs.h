#pragma once

// プラットフォーム選択マクロ。
// AQ_PLATFORM_WIN32 / AQ_PLATFORM_UWP / AQ_PLATFORM_MAC / AQ_PLATFORM_ANDROID の
// うち ちょうど 1 つだけが定義された状態を保証する。
// 通常はプロジェクト設定(vcxproj / CMake)で定義し、未定義ならコンパイラの
// 定義済みマクロから推定する。
//
// 最初期(aq.h の先頭)に読まれるため、他のヘッダを一切 include しないこと。

#if !defined(AQ_PLATFORM_WIN32) && !defined(AQ_PLATFORM_UWP) && !defined(AQ_PLATFORM_MAC) && !defined(AQ_PLATFORM_ANDROID)
// __ANDROID__ は __linux__ も一緒に定義されるため、Linux より先に判定する。
#if defined(__ANDROID__)
#define AQ_PLATFORM_ANDROID
#elif defined(_WIN32)
#define AQ_PLATFORM_WIN32
#elif defined(__APPLE__)
#define AQ_PLATFORM_MAC
#else
#error "Define exactly one AQ_PLATFORM_* platform"
#endif
#endif

#if (defined(AQ_PLATFORM_WIN32) + defined(AQ_PLATFORM_UWP) + defined(AQ_PLATFORM_MAC) + defined(AQ_PLATFORM_ANDROID)) > 1
#error "Define exactly one AQ_PLATFORM_* platform"
#endif


// 派生マクロ。
//  AQ_PLATFORM_DESKTOP        : デスクトップ OS (Win32 / Mac)
//  AQ_PLATFORM_WINDOWS_FAMILY : windows.h 系 API が使える (Win32 / UWP)
//
// Android はどちらにも入らない。ウィンドウ/ファイルシステム/入力の前提が
// デスクトップと異なるため、「デスクトップでない」ことを型で効かせる。
#if defined(AQ_PLATFORM_WIN32) || defined(AQ_PLATFORM_MAC)
#define AQ_PLATFORM_DESKTOP
#endif

#if defined(AQ_PLATFORM_WIN32) || defined(AQ_PLATFORM_UWP)
#define AQ_PLATFORM_WINDOWS_FAMILY
#endif
