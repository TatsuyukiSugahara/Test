# ============================================================================
#  Game(AquaDash 実行ファイル)
#
#  ソース集合は Game/GameSources.props と同じ。ただし UWP 専用エントリの
#  Application/UWPMain.cpp は CMake では扱わない(UWP は GameUWP.vcxproj の
#  経路)。Application/Main.cpp は AQ_PLATFORM_WIN32 でガードされているため、
#  Mac では空 TU になる。
# ============================================================================

file(GLOB_RECURSE AQ_GAME_SOURCES CONFIGURE_DEPENDS
	"${CMAKE_CURRENT_SOURCE_DIR}/Application/*.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/Application/*.mm"
)
file(GLOB_RECURSE AQ_GAME_HEADERS CONFIGURE_DEPENDS
	"${CMAKE_CURRENT_SOURCE_DIR}/Application/*.h"
)

aq_exclude_sources(AQ_GAME_SOURCES
	"/Application/UWPMain\\.cpp$"
)

if(NOT APPLE)
	aq_exclude_sources(AQ_GAME_SOURCES "\\.mm$")
endif()


# ----------------------------------------------------------------------------
#  実行ファイル
#
#  Windows は WIN32(SubSystem=Windows / WinMain)。既存 DirectX.vcxproj の
#  x64 構成の SubSystem と同じ。
#  Mac は .app バンドル。バンドルへの Assets 同梱は P5 の作業(設計書 §9)。
# ----------------------------------------------------------------------------
add_executable(Game WIN32 MACOSX_BUNDLE ${AQ_GAME_SOURCES} ${AQ_GAME_HEADERS})

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${AQ_GAME_SOURCES} ${AQ_GAME_HEADERS})

aq_apply_common_compile_options(Game)

target_include_directories(Game PRIVATE
	"${CMAKE_CURRENT_SOURCE_DIR}"
	"${CMAKE_CURRENT_SOURCE_DIR}/Application"
)

# aqEngine が PUBLIC で持つ定義(AQ_PLATFORM_* / ENGINE_GRAPHICS_* / AQ_IMGUI /
# BT_*)とインクルードパス、プラットフォームライブラリはここで伝播してくる。
target_link_libraries(Game PRIVATE aqEngine)

# DirectX.vcxproj の PrecompiledHeaderFile = stdafx.h に対応。
target_precompile_headers(Game PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Application/stdafx.h")

if(MSVC)
	# Bullet の prebuilt .lib に PDB が揃っていないため出る LNK4099 を抑止する
	# (DirectX.vcxproj の Link/AdditionalOptions と同じ)。
	target_link_options(Game PRIVATE /ignore:4099)
endif()


# ----------------------------------------------------------------------------
#  デバッグ実行時の作業ディレクトリ
#
#  既存 vcxproj は LocalDebuggerWorkingDirectory を明示していないため、既定の
#  $(ProjectDir) = Game/ が使われる。Resource.cpp の FindProjectRoot() は
#  カレントディレクトリから上へ Game/Assets を探すので、ここを合わせないと
#  アセットが見つからない。
# ----------------------------------------------------------------------------
set_target_properties(Game PROPERTIES
	VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/Game"
)


# ----------------------------------------------------------------------------
#  Vulkan 構成の実行時 DLL
#
#  DirectX.vcxproj の PostBuildEvent と同じ。VulkanShader.cpp が実行時に
#  dxcompiler.dll(と dxil.dll)を必要とするため出力先へコピーする。
# ----------------------------------------------------------------------------
if(WIN32 AND AQ_GRAPHICS_API STREQUAL "Vulkan")
	foreach(vulkanDll dxcompiler.dll dxil.dll)
		if(EXISTS "${AQ_VULKAN_SDK}/Bin/${vulkanDll}")
			add_custom_command(TARGET Game POST_BUILD
				COMMAND ${CMAKE_COMMAND} -E copy_if_different
					"${AQ_VULKAN_SDK}/Bin/${vulkanDll}"
					"$<TARGET_FILE_DIR:Game>"
				COMMENT "Vulkan ランタイム DLL をコピー: ${vulkanDll}"
			)
		endif()
	endforeach()
endif()
