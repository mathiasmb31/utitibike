
cmake_minimum_required(VERSION 3.16.3)
# La version est déclarée ici, unique source de vérité :
#  - manifest.json est généré depuis manifest.json.in (clickable le lit à la racine)
#  - UTITIBIKE_VERSION est injecté en C++ (target_compile_definitions)
#  - le QML la récupère via le context property "appVersion" (main.cpp)
project(utitibike VERSION 0.1.14 LANGUAGES CXX)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/manifest.json.in
               ${CMAKE_CURRENT_SOURCE_DIR}/manifest.json @ONLY)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt5 5.12 REQUIRED COMPONENTS Core Gui Quick Network DBus Positioning Multimedia)

set(SRC
    src/main.cpp
    src/appdata.h
    src/appdata.cpp
    src/tilecache.h
    src/tilecache.cpp
    src/tilemapitem.h
    src/tilemapitem.cpp
    src/brouterclient.h
    src/brouterclient.cpp
    src/gpstracker.h
    src/gpstracker.cpp
    src/routerecorder.h
    src/routerecorder.cpp
    src/routehistory.h
    src/routehistory.cpp
    src/favorites.h
    src/favorites.cpp
    src/urlopener.h
    src/haptics.h
    src/appstate.h
    src/displaykeeper.h
    src/geo.h
    src/geoutils.h
    src/geoutils.cpp
    src/viewstate.h
    src/viewstate.cpp
    src/searchclient.h
    src/searchclient.cpp
    src/daemoncontroller.h
    src/daemoncontroller.cpp
)

qt5_add_resources(QRC_SOURCES qml.qrc)

add_executable(utitibike ${SRC} ${QRC_SOURCES})

target_compile_definitions(utitibike PRIVATE UTITIBIKE_VERSION="${PROJECT_VERSION}")

target_link_libraries(utitibike Qt5::Core Qt5::Gui Qt5::Quick Qt5::Network Qt5::DBus Qt5::Positioning Qt5::Multimedia)

# --- Daemon GPS détaché ---
# Exécutable secondaire dans le même click. Lancé par l'app QML via
# QProcess::startDetached() — échappe au SIGSTOP de Lomiri.
# Enregistre le GPS sans UI, écrit les stats en JSON, beep toutes les 10 s.
set(GPSD_SRC
    src/gpsdaemon/main.cpp
    src/gpsdaemon/gpsdaemon.h
    src/gpsdaemon/gpsdaemon.cpp
    src/geo.h
    src/appdata.h
    src/appdata.cpp
)

add_executable(utitibike-gpsd ${GPSD_SRC})

target_include_directories(utitibike-gpsd PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(utitibike-gpsd Qt5::Core Qt5::Positioning Qt5::Multimedia)

install(TARGETS utitibike utitibike-gpsd DESTINATION .)
install(FILES manifest.json utitibike.apparmor utitibike.desktop logo.png DESTINATION .)
# beep.wav installé comme fichier séparé (pas dans le qrc) pour que le daemon
# puisse y accéder via applicationDirPath()/sounds/beep.wav.
install(FILES sounds/beep.wav DESTINATION sounds/)

# --- Tests unitaires ---
# OFF par défaut : le build clickable (cross-compile ARM64) ne les compile pas.
# Le CI host passe -DBUILD_TESTING=ON pour les builder et les lancer.
option(BUILD_TESTING "Build unit tests" OFF)
if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()

# --- Internationalisation ---
# Deux mécanismes, deux formats :
#  - QML (i18n.tr = gettext/libintl) : po/utitibike_<lang>.po -> .mo
#    installé dans share/locale/<lang>/LC_MESSAGES/utitibike.mo
#    (i18n.domain = "utitibike" fait bindtextdomain vers $APP_DIR/share/locale)
#  - C++ (QObject::tr = QTranslator/Qt) : po/utitibike-cpp_<lang>.po -> .ts
#    -> .qm à la racine du click. msgfmt --qt est inutilisable (vieux format .qm
#    sans Unicode dans les msgid), d'où la conversion .po -> .ts (po2ts.py)
#    puis lrelease.
find_program(MSGFMT_EXECUTABLE msgfmt)
find_program(LRELEASE_EXECUTABLE lrelease)
find_program(PYTHON3_EXECUTABLE python3)

if(MSGFMT_EXECUTABLE)
    file(GLOB MO_PO_FILES ${CMAKE_CURRENT_SOURCE_DIR}/po/utitibike_??.po)
    set(MO_TARGET_FILES "")
    foreach(PO_FILE ${MO_PO_FILES})
        get_filename_component(PO_NAME ${PO_FILE} NAME_WE)
        string(REPLACE "utitibike_" "" MO_LANG ${PO_NAME})
        set(MO_FILE ${CMAKE_CURRENT_BINARY_DIR}/${PO_NAME}.mo)
        add_custom_command(
            OUTPUT ${MO_FILE}
            COMMAND ${MSGFMT_EXECUTABLE} -o ${MO_FILE} ${PO_FILE}
            DEPENDS ${PO_FILE}
            COMMENT "msgfmt ${PO_NAME}.po"
        )
        install(FILES ${MO_FILE} DESTINATION share/locale/${MO_LANG}/LC_MESSAGES RENAME utitibike.mo)
        list(APPEND MO_TARGET_FILES ${MO_FILE})
    endforeach()
    add_custom_target(mo ALL DEPENDS ${MO_TARGET_FILES})
else()
    message(WARNING "msgfmt not found — .mo translations will NOT be built")
endif()

if(LRELEASE_EXECUTABLE AND PYTHON3_EXECUTABLE)
    file(GLOB QM_PO_FILES ${CMAKE_CURRENT_SOURCE_DIR}/po/utitibike-cpp_??.po)
    set(QM_FILES "")
    foreach(PO_FILE ${QM_PO_FILES})
        get_filename_component(PO_NAME ${PO_FILE} NAME_WE)
        string(REPLACE "utitibike-cpp_" "" QM_LANG ${PO_NAME})
        set(TS_FILE ${CMAKE_CURRENT_BINARY_DIR}/${PO_NAME}.ts)
        set(QM_FILE ${CMAKE_CURRENT_BINARY_DIR}/utitibike_${QM_LANG}.qm)
        add_custom_command(
            OUTPUT ${QM_FILE}
            COMMAND ${PYTHON3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/po/po2ts.py ${PO_FILE} ${TS_FILE}
            COMMAND ${LRELEASE_EXECUTABLE} ${TS_FILE} -qm ${QM_FILE}
            DEPENDS ${PO_FILE} ${CMAKE_CURRENT_SOURCE_DIR}/po/po2ts.py
            COMMENT "lrelease ${PO_NAME}.po"
        )
        list(APPEND QM_FILES ${QM_FILE})
    endforeach()
    add_custom_target(qm ALL DEPENDS ${QM_FILES})
    install(FILES ${QM_FILES} DESTINATION .)
else()
    message(WARNING "lrelease or python3 not found — .qm translations will NOT be built")
endif()

