qt_add_executable(wivrn-server-gui
        main.cpp
        main_window.cpp
        main_window.h
        settings.cpp
        settings.h
        rectangle_partitionner.cpp
        rectangle_partitionner.h
        ../server-gui.qrc
        ${CMAKE_CURRENT_BINARY_DIR}/../common/version.cpp
)

set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/../dbus/io.github.wivrn.Server.xml PROPERTIES INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/wivrn_qdbus_types.h")
qt_add_dbus_interface(DBUS_INTERFACE_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../dbus/io.github.wivrn.Server.xml wivrn_server)
target_sources(wivrn-server-gui PRIVATE ${DBUS_INTERFACE_SRC})

target_compile_features(wivrn-server-gui PRIVATE cxx_std_20)
target_include_directories(wivrn-server-gui PRIVATE ${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(wivrn-server-gui PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::DBus
)

file(GLOB TRANSLATION_FILES ${CMAKE_CURRENT_SOURCE_DIR}/wivrn_*.ts)
qt_add_translations(wivrn-server-gui TS_FILES ${TRANSLATION_FILES})

install(TARGETS wivrn-server-gui)
