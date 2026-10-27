find_package(Qt5 REQUIRED COMPONENTS Test)

# UTITIBIKE_VERSION est injecté par le CMakeLists parent pour l'app,
# il faut aussi le définir pour les tests (brouterclient.cpp l'utilise).
add_compile_definitions(UTITIBIKE_VERSION="${PROJECT_VERSION}")

# Sources communes testées (sans main.cpp ni les trucs dépendants du device)
set(COMMON_SRC
    ${CMAKE_SOURCE_DIR}/src/appdata.cpp
    ${CMAKE_SOURCE_DIR}/src/tilemapitem.cpp
    ${CMAKE_SOURCE_DIR}/src/brouterclient.cpp
    ${CMAKE_SOURCE_DIR}/src/favorites.cpp
    ${CMAKE_SOURCE_DIR}/src/routehistory.cpp
    ${CMAKE_SOURCE_DIR}/src/viewstate.cpp
    ${CMAKE_SOURCE_DIR}/src/geoutils.cpp
    ${CMAKE_SOURCE_DIR}/src/tilecache.cpp
)

# geo.h est header-only : test sans sources externes, GUILESS
add_executable(tst_geo tst_geo.cpp)
target_include_directories(tst_geo PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(tst_geo Qt5::Test Qt5::Core)
add_test(NAME geo COMMAND tst_geo)

# TileMapItem est un QQuickPaintedItem : QTEST_MAIN crée un QGuiApplication
add_executable(tst_tilemapitem tst_tilemapitem.cpp ${COMMON_SRC})
target_include_directories(tst_tilemapitem PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(tst_tilemapitem Qt5::Test Qt5::Core Qt5::Gui Qt5::Quick Qt5::Network)
add_test(NAME tilemapitem COMMAND tst_tilemapitem)

# BrouterClient : QCoreApplication suffit (pas de GUI)
add_executable(tst_brouterclient tst_brouterclient.cpp
    ${CMAKE_SOURCE_DIR}/src/brouterclient.cpp
    ${CMAKE_SOURCE_DIR}/src/geoutils.cpp
)
target_include_directories(tst_brouterclient PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(tst_brouterclient Qt5::Test Qt5::Core Qt5::Network)
add_test(NAME brouterclient COMMAND tst_brouterclient)

# Persistance (Favorites, RouteHistory, ViewState)
add_executable(tst_persistence tst_persistence.cpp
    ${CMAKE_SOURCE_DIR}/src/appdata.cpp
    ${CMAKE_SOURCE_DIR}/src/favorites.cpp
    ${CMAKE_SOURCE_DIR}/src/routehistory.cpp
    ${CMAKE_SOURCE_DIR}/src/viewstate.cpp
    ${CMAKE_SOURCE_DIR}/src/geoutils.cpp
)
target_include_directories(tst_persistence PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(tst_persistence Qt5::Test Qt5::Core)
add_test(NAME persistence COMMAND tst_persistence)
