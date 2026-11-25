# ============================================================================
#  Editor(エディタ実行ファイル。AQ_BUILD_EDITOR=ON のときだけルートから追加される)
#
#  設計書/エディタ/E10_Macのエディタ.md E10a。Mac(Metal)専用。
#  ソースは Editor/ の .cpp / .mm を再帰で集める。EditorMain.cpp は AQ_PLATFORM_WIN32 で
#  囲まれているので Mac では空 TU になる。Editor.vcxproj には Mac 専用のファイル
#  (EditorMacMain.mm)を登録しない。
# ============================================================================

file(GLOB_RECURSE AQ_EDITOR_SOURCES CONFIGURE_DEPENDS
	"${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/*.mm"
)
file(GLOB_RECURSE AQ_EDITOR_HEADERS CONFIGURE_DEPENDS
	"${CMAKE_CURRENT_SOURCE_DIR}/*.h"
)

add_executable(Editor MACOSX_BUNDLE ${AQ_EDITOR_SOURCES} ${AQ_EDITOR_HEADERS})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${AQ_EDITOR_SOURCES} ${AQ_EDITOR_HEADERS})

# CFBundleIdentifier が空の .app はキーウィンドウにならず、キー入力が届かない
# (Game/CMakeLists.txt と同じ)。
set_target_properties(Editor PROPERTIES
	MACOSX_BUNDLE_BUNDLE_NAME          "aqEditor"
	MACOSX_BUNDLE_GUI_IDENTIFIER       "com.aqengine.editor"
	MACOSX_BUNDLE_BUNDLE_VERSION       "1.0"
	MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0"
)

aq_apply_common_compile_options(Editor)

# stdafx.h などの include と AQ_EDITOR 等の定義は GameLogic(と aqEngine)から PUBLIC で伝わる。
# PCH は使わない(Editor の .cpp は stdafx.h を普通に include する)。
target_include_directories(Editor PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(Editor PRIVATE GameLogic)

# 作業ディレクトリは Game/(FindProjectRoot が CWD から上へ Game/Assets を探すため。Game と同じ)。
set_target_properties(Editor PROPERTIES
	VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/Game"
)
if(CMAKE_GENERATOR STREQUAL "Xcode")
	set_target_properties(Editor PROPERTIES
		XCODE_GENERATE_SCHEME         ON
		XCODE_SCHEME_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/Game"
	)
endif()
