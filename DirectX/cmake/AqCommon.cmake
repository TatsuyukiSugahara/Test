# ============================================================================
#  CMake 共通ヘルパ
#
#  ルート CMakeLists.txt から include して使う。ターゲットごとに繰り返し書く
#  コンパイルオプション(C++20 / /utf-8 / 警告レベル等)と、プラットフォーム
#  選択マクロ(AQ_PLATFORM_*)の付与をここへ集約する。
# ============================================================================


# ----------------------------------------------------------------------------
#  共通コンパイル設定を対象ターゲットへ適用する
#
#  既存 vcxproj(Debug|x64 / Release|x64)の ClCompile 設定に対応:
#    LanguageStandard   = stdcpp20   -> cxx_std_20
#    AdditionalOptions  = /utf-8     -> /utf-8
#    WarningLevel       = Level3     -> /W3
#    ConformanceMode    = true       -> /permissive-  (MSVC のみ)
#    SDLCheck           = true       -> /sdl          (MSVC のみ)
#    FunctionLevelLinking / IntrinsicFunctions (Release) -> /Gy /Oi
#
#  clang-cl は CMAKE_CXX_COMPILER_ID が "Clang" になるため、MSVC 固有の
#  /permissive- と /sdl は付けない(MSVC 判定のジェネレータ式で分岐する)。
# ----------------------------------------------------------------------------
function(aq_apply_common_compile_options targetName)
	target_compile_features(${targetName} PRIVATE cxx_std_20)

	if(MSVC)
		# MSVC / clang-cl 共通(if(MSVC) は clang-cl でも真になる)
		target_compile_options(${targetName} PRIVATE
			/utf-8
			/W3
			$<$<CONFIG:Release>:/Gy>
			$<$<CONFIG:Release>:/Oi>
		)
		# MSVC 本体でのみ有効なオプション
		target_compile_options(${targetName} PRIVATE
			$<$<CXX_COMPILER_ID:MSVC>:/permissive->
			$<$<CXX_COMPILER_ID:MSVC>:/sdl>
		)
	else()
		target_compile_options(${targetName} PRIVATE -Wall)
	endif()
endfunction()


# ----------------------------------------------------------------------------
#  プラットフォーム選択マクロを対象ターゲットへ付与する
#
#  aqEngine/Platform/Common/PlatformDefs.h は AQ_PLATFORM_WIN32 /
#  AQ_PLATFORM_UWP / AQ_PLATFORM_MAC のちょうど 1 つを要求する。
#  UWP は vcxproj 専用(GameUWP.vcxproj)なので CMake では扱わない。
#
#  visibility には PUBLIC を渡すこと。aq.h を include する側(Game)にも
#  同じマクロが見えている必要がある。
# ----------------------------------------------------------------------------
function(aq_apply_platform_definitions targetName visibility)
	if(WIN32)
		target_compile_definitions(${targetName} ${visibility} AQ_PLATFORM_WIN32 _WINDOWS _CRT_SECURE_NO_WARNINGS)
	elseif(APPLE)
		target_compile_definitions(${targetName} ${visibility} AQ_PLATFORM_MAC)
	else()
		message(FATAL_ERROR "サポート外のプラットフォームです (Windows / macOS のみ)")
	endif()
endfunction()


# ----------------------------------------------------------------------------
#  ソース一覧から除外パターンに一致するものを取り除く
#
#  file(GLOB_RECURSE) の結果から、プラットフォーム別・構成別に不要なファイルを
#  落とすために使う。パターンは正規表現(list(FILTER ... REGEX))。
#
#  @param resultVar  結果を書き戻す変数名
#  @param ARGN       除外する正規表現(複数可)
# ----------------------------------------------------------------------------
function(aq_exclude_sources resultVar)
	set(filtered ${${resultVar}})
	foreach(pattern IN LISTS ARGN)
		list(FILTER filtered EXCLUDE REGEX "${pattern}")
	endforeach()
	set(${resultVar} ${filtered} PARENT_SCOPE)
endfunction()
