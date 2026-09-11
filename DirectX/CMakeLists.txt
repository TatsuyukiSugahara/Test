# ============================================================================
#  aqEngine + AquaDash の CMake ビルド定義(ルート)
#
#  既存の DirectX.sln / *.vcxproj とは「併存」する。Windows 開発の主経路は
#  当面 vcxproj のままで、本ファイルは Mac 移植(設計書/Mac移植設計.md §9 P0)
#  のビルド基盤として追加したもの。
#
#  生成例:
#    cmake --preset windows-vs2026      … VS プロジェクトを生成
#    cmake --preset windows-clang-cl    … Ninja + clang-cl 構成を生成
#    cmake --preset macos-ninja         … Mac(検証は P2)
#  導入手順とハマりどころは Tools/SetupCMake/README.md を参照。
# ============================================================================
cmake_minimum_required(VERSION 3.21)

project(AquaDash LANGUAGES C CXX)

# Objective-C++(.mm)は Mac のみ。Platform/Mac・HID/Mac・Sound/CoreAudio で使う。
if(APPLE)
	enable_language(OBJCXX)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(AqCommon)


# ----------------------------------------------------------------------------
#  構成(Debug / Release)
#
#  vcxproj には DebugXbox / ReleaseXbox(UWP)もあるが、UWP は vcxproj 専用の
#  経路なので CMake では扱わない(設計書 §0「UWP は CMake では扱わない」)。
# ----------------------------------------------------------------------------
if(CMAKE_CONFIGURATION_TYPES)
	set(CMAKE_CONFIGURATION_TYPES "Debug;Release" CACHE STRING "対応する構成" FORCE)
elseif(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "構成 (Debug / Release)" FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)


# ----------------------------------------------------------------------------
#  グラフィックス API の選択
#
#  MSBuild 側のプロパティ AqGraphicsApi(Game/GraphicsApi.props)と同じ意味・
#  同じ既定値。ENGINE_GRAPHICS_<API> の定義と API 別ライブラリのリンクを決める。
# ----------------------------------------------------------------------------
set(AQ_GRAPHICS_API "D3D12" CACHE STRING "グラフィックス API (D3D11 / D3D12 / Vulkan / Metal)")
set_property(CACHE AQ_GRAPHICS_API PROPERTY STRINGS "D3D11" "D3D12" "Vulkan" "Metal")

if(NOT AQ_GRAPHICS_API MATCHES "^(D3D11|D3D12|Vulkan|Metal)$")
	message(FATAL_ERROR "AQ_GRAPHICS_API は D3D11 / D3D12 / Vulkan / Metal のいずれかを指定してください (現在: ${AQ_GRAPHICS_API})")
endif()

if(NOT WIN32 AND AQ_GRAPHICS_API MATCHES "^D3D")
	message(FATAL_ERROR "Windows 以外では AQ_GRAPHICS_API=Vulkan / Metal のみ指定できます (現在: ${AQ_GRAPHICS_API})")
endif()

if(AQ_GRAPHICS_API STREQUAL "Metal" AND NOT APPLE)
	message(FATAL_ERROR "AQ_GRAPHICS_API=Metal は macOS 専用です")
endif()

message(STATUS "AquaDash: AQ_GRAPHICS_API = ${AQ_GRAPHICS_API}")


# ----------------------------------------------------------------------------
#  Vulkan SDK の解決
#
#  AQ_GRAPHICS_API=Vulkan のときだけ環境変数 VULKAN_SDK を要求する。それ以外の
#  構成ではこの分岐に入らないので、SDK 未導入の環境でも configure は成功する。
#  解決結果 AQ_VULKAN_SDK は aqEngine(ヘッダ/ライブラリ)と Game(実行時 DLL の
#  コピー)の双方から参照するため、ルートで一度だけ決める。
# ----------------------------------------------------------------------------
#  Metal 構成も SDK を要求する。描画には使わないが、シェーダのビルド経路
#  (.fx -> dxc -> .spv -> spirv-cross -> .metal) がどちらも SDK 同梱のツールのため
#  (設計書/MetalBackend設計.md §9.1)。
set(AQ_VULKAN_SDK "")
if(AQ_GRAPHICS_API STREQUAL "Vulkan" OR AQ_GRAPHICS_API STREQUAL "Metal")
	if(NOT DEFINED ENV{VULKAN_SDK})
		message(FATAL_ERROR "AQ_GRAPHICS_API=${AQ_GRAPHICS_API} には環境変数 VULKAN_SDK が必要です (dxc / spirv-cross の入手元)")
	endif()
	file(TO_CMAKE_PATH "$ENV{VULKAN_SDK}" AQ_VULKAN_SDK)
	message(STATUS "AquaDash: VULKAN_SDK = ${AQ_VULKAN_SDK}")
endif()


# ----------------------------------------------------------------------------
#  MSVC ランタイムライブラリ
#
#  既存 vcxproj に合わせる(Debug = /MTd、Release = /MD)。ThirdParty の
#  prebuilt Bullet(lib/Debug は /MTd ビルド)と CRT を一致させるために必要。
#  CMP0091(3.15 以降)が NEW のため、この変数で一括指定できる。
# ----------------------------------------------------------------------------
if(MSVC)
	set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>$<$<NOT:$<CONFIG:Debug>>:DLL>")
endif()


# ----------------------------------------------------------------------------
#  出力先
#
#  既存 MSBuild の出力(<root>/x64/<Config>)とは別の場所へ出す。同じ場所へ
#  出すと vcxproj ビルドの成果物を上書きしてしまうため。
# ----------------------------------------------------------------------------
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib/$<CONFIG>")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib/$<CONFIG>")


# ----------------------------------------------------------------------------
#  サブディレクトリ(依存順)
# ----------------------------------------------------------------------------
add_subdirectory(ThirdParty)
add_subdirectory(aqEngine)
add_subdirectory(Game)

# 開発用ツール(CMake 専用。vcxproj は用意しない)
add_subdirectory(Tools/MixerTest)

# ----------------------------------------------------------------------------
#  ビルド時 SPIR-V 生成(設計書 Mac移植設計.md §4)
#
#  Mac は実行時 DXC 経路を持たないので .spv の事前生成が必須。
#  Windows Vulkan 構成では「.spv あり/なし」の比較検証に使う。
#  dxc は Vulkan SDK 同梱のため、SDK を要求しない構成(既定の D3D12 等)では
#  配線しない(compile_spv.cmake は dxc が無いと FATAL_ERROR で止まるため)。
# ----------------------------------------------------------------------------
if(AQ_GRAPHICS_API STREQUAL "Vulkan")
	include(${CMAKE_CURRENT_SOURCE_DIR}/Tools/ShaderCompile/compile_spv.cmake)
	aq_add_compile_spv_target(aqCompileSpv)
	add_dependencies(Game aqCompileSpv)
endif()

# ----------------------------------------------------------------------------
#  ビルド時 MSL 生成(設計書 MetalBackend設計.md §9.1)
#
#  Metal には実行時 HLSL コンパイル経路が無いので、.fx -> dxc -> .spv ->
#  spirv-cross -> .metal をビルド時に通す。実行時にコンパイルするのは MSL のほう
#  (MetalShader が newLibraryWithSource: で行う)。
#  dxc / spirv-cross はどちらも Vulkan SDK 同梱のため、Metal 構成でも
#  VULKAN_SDK を要求している(上の AQ_VULKAN_SDK の解決を参照)。
#  シフトが Vulkan と違うので、出力先も Game/Assets/Shader/msl と別にしてある。
# ----------------------------------------------------------------------------
if(AQ_GRAPHICS_API STREQUAL "Metal")
	include(${CMAKE_CURRENT_SOURCE_DIR}/Tools/ShaderCompile/compile_msl.cmake)
	aq_add_compile_msl_target(aqCompileMsl)
	add_dependencies(Game aqCompileMsl)
endif()

# VS 生成時のスタートアッププロジェクト
if(CMAKE_GENERATOR MATCHES "Visual Studio")
	set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT Game)
endif()
