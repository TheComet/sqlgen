cmake_minimum_required (VERSION 3.21)

project (example
    LANGUAGES C
    VERSION 1.0.0)

# Fetch sqlite3 and compile
include (FetchContent)
FetchContent_Declare (
    sqlite3
    URL https://www.sqlite.org/2023/sqlite-amalgamation-3440200.zip
    URL_HASH SHA256=833be89b53b3be8b40a2e3d5fedb635080e3edb204957244f3d6987c2bb2345f
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable (sqlite3)
FetchContent_GetProperties (sqlite3)
add_library (sqlite3 STATIC
    "${sqlite3_SOURCE_DIR}/sqlite3.c"
    "${sqlite3_SOURCE_DIR}/sqlite3.h")
target_include_directories (sqlite3 PUBLIC "$<BUILD_INTERFACE:${sqlite3_SOURCE_DIR}>")
target_compile_definitions (sqlite3 PRIVATE SQLITE_THREADSAFE=0)

# Adding sqlgen as a subdirectory gives access to the "sqlgen_target" macro
add_subdirectory ("../sqlgen" "${PROJECT_BINARY_DIR}/sqlgen")

sqlgen_target (mydb
    INPUT "mydb.sqlgen"
    BACKENDS sqlite3)

add_executable (example
    "mydb.sqlgen"
    "main.c"
    "${SQLGEN_mydb_OUTPUTS}")
target_link_libraries (example PRIVATE sqlite3)
# mydb.sqlgen.h is placed here
target_include_directories (example PRIVATE ${PROJECT_BINARY_DIR})

set_property(
    DIRECTORY ${PROJECT_SOURCE_DIR}
    PROPERTY VS_STARTUP_PROJECT example)
