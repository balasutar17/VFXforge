# The desktop app: a window over the editor layer.
#
# This is the only part of VFX Forge that needs Qt. It is built when
# VFX_BUILD_APP is ON; the core, the editor layer and all their tests build
# without it.
find_package(Qt6 6.5 REQUIRED COMPONENTS Gui Qml Quick QuickControls2 ShaderTools Multimedia)

set(CMAKE_AUTOMOC ON)

qt_add_executable(vfxforge WIN32 MACOSX_BUNDLE
    src/main.cpp
    src/AppController.cpp
    src/AppController.h
    src/AudioPlayer.cpp
    src/AudioPlayer.h
    src/PreviewItems.cpp
    src/PreviewItems.h
    src/ReferenceController.cpp
    src/ReferenceController.h
    src/SpriteNode.cpp
    src/SpriteNode.h
    src/ViewportItem.cpp
    src/ViewportItem.h
)

target_link_libraries(vfxforge PRIVATE
    vfx::editor
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Multimedia
)
target_compile_definitions(vfxforge PRIVATE VFX_APP_VERSION="${PROJECT_VERSION}")

# The window, described in QML, travels inside the program.
qt_add_resources(vfxforge "vfxforge_qml"
    PREFIX "/"
    FILES
        qml/Main.qml
        qml/PanelTitle.qml
        qml/VButton.qml
        qml/VCheck.qml
        qml/VNumberField.qml
        qml/VSlider.qml
        qml/VTextField.qml
        qml/ControlHeader.qml
        qml/ControlNumber.qml
        qml/ControlRange.qml
        qml/ControlColor.qml
        qml/ControlDirection.qml
        qml/ControlNote.qml
        qml/ControlChoice.qml
        qml/ControlShape.qml
        qml/Library.qml
        qml/ExportPanel.qml
        qml/ControlSound.qml
        qml/ReferenceWorkspace.qml
        samples/toon-flame-4x2.png
        samples/reference-flash.png
        samples/reference-burst.gif
        samples/reference-burst.mp4
        samples/reference-burst.webm
)

# One shader source, translated at build time for Metal, Direct3D and OpenGL.
# BATCHABLE is required for shaders used by a Qt Quick material.
qt_add_shaders(vfxforge "vfxforge_shaders"
    BATCHABLE
    PREFIX "/"
    FILES
        shaders/sprite.vert
        shaders/sprite.frag
        shaders/image.frag
)

set_target_properties(vfxforge PROPERTIES
    OUTPUT_NAME "VFX Forge"
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/packaging/Info.plist.in"
    MACOSX_BUNDLE_BUNDLE_NAME "VFX Forge"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.vfxforge.app"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    MACOSX_BUNDLE_ICON_FILE "VFXForge.icns"
)

if(APPLE)
    target_sources(vfxforge PRIVATE packaging/VFXForge.icns)
    set_source_files_properties(packaging/VFXForge.icns PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources")
elseif(WIN32)
    target_sources(vfxforge PRIVATE packaging/VFXForge.rc)
endif()
