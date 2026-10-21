cmake_minimum_required(VERSION 3.19)
project(harbour-sailrss CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package (Qt5 COMPONENTS Core Network Sql Qml Gui Quick LinguistTools REQUIRED)

include(FindPkgConfig)
pkg_search_module(SAILFISH sailfishapp REQUIRED)

set(CMAKE_AUTOMOC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

# Code shared by the app and the tests
add_library(sailrss-core STATIC
    src/api/greaderclient.cpp
    src/api/greaderclient.h
    src/api/greaderparser.cpp
    src/api/greaderparser.h
    src/api/greadertypes.h
    src/storage/database.cpp
    src/storage/database.h
    src/sync/syncmanager.cpp
    src/sync/syncmanager.h
)
target_include_directories(sailrss-core PUBLIC src)
target_link_libraries(sailrss-core PUBLIC Qt5::Core Qt5::Network Qt5::Sql)

add_executable(harbour-sailrss src/harbour-sailrss.cpp)
target_compile_definitions(harbour-sailrss PRIVATE
    $<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:QT_QML_DEBUG>
)
target_include_directories(harbour-sailrss PRIVATE
    $<BUILD_INTERFACE:
    ${SAILFISH_INCLUDE_DIRS}
>)
target_link_libraries(harbour-sailrss
    sailrss-core
    Qt5::Quick
    ${SAILFISH_LDFLAGS}
)

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

install(TARGETS harbour-sailrss
    RUNTIME DESTINATION bin
)
install(DIRECTORY qml
    DESTINATION share/harbour-sailrss
)
# Compile translations; SailfishApp loads them from share/harbour-sailrss/translations
file(GLOB TsFiles "translations/*.ts")
qt5_add_translation(QmFiles ${TsFiles})
add_custom_target(translations ALL DEPENDS ${QmFiles})
install(FILES ${QmFiles}
    DESTINATION share/harbour-sailrss/translations
)
install(FILES harbour-sailrss.desktop
    DESTINATION share/applications
)
install(FILES icons/86x86/harbour-sailrss.png
    DESTINATION share/icons/hicolor/86x86/apps
)
install(FILES icons/108x108/harbour-sailrss.png
    DESTINATION share/icons/hicolor/108x108/apps
)
install(FILES icons/128x128/harbour-sailrss.png
    DESTINATION share/icons/hicolor/128x128/apps
)
install(FILES icons/172x172/harbour-sailrss.png
    DESTINATION share/icons/hicolor/172x172/apps
)

# Get the other files reachable from the project tree in Qt Creator
add_custom_target(distfiles
    SOURCES
        harbour-sailrss.desktop
        qml/harbour-sailrss.qml
        qml/cover/CoverPage.qml
        qml/pages/MainPage.qml
        rpm/harbour-sailrss.spec
        ${TsFiles})

# Tell Qt Creator where the application executable(s) would be located on the
# device.
#
# It is not necessary to list other deployables than executables (runtime
# targets) here. The deployment process of Sailfish OS projects is opaque to
# Qt Creator and the information contained in QtCreatorDeployment.txt is only
# used to locate the executable associated with the active run configuration
# on the device in order to run it.
#
# Search the Qt Creator Manual to learn about the QtCreatorDeployment.txt file
# format.
file(WRITE "${CMAKE_BINARY_DIR}/QtCreatorDeployment.txt"
    "${CMAKE_INSTALL_PREFIX}\n${CMAKE_BINARY_DIR}/harbour-sailrss:bin\n")

