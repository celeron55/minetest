project(sailfish_inputprocess)
add_executable(sailfish_inputprocess sailfish_inputprocess.cpp moc_sailfish_inputprocess.cpp)
target_link_libraries(sailfish_inputprocess -lQt5Core -lQt5DBus -lQt5Gui -lQt5Quick -lQt5Qml -lsailfishapp)
add_custom_command(OUTPUT moc_sailfish_inputprocess.cpp
		COMMAND moc ${CMAKE_SOURCE_DIR}/src/sailfish/inputprocess/sailfish_inputprocess.h -o ${PROJECT_BINARY_DIR}/moc_sailfish_inputprocess.cpp
		DEPENDS ${CMAKE_SOURCE_DIR}/src/sailfish/inputprocess/sailfish_inputprocess.h COMMENT "moc")
include_directories("/usr/include/qt5")
include_directories("/usr/include/qt5/QtCore")
# -fPIC needed for Qt
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -fPIC")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fPIC")

