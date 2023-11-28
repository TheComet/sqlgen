cmake_minimum_required (VERSION 3.21)

project (example
    LANGUAGES C
    VERSION 1.0.0)

# You will need to download the sqlite3 amalgamation and place these files
# accordingly (they are not included in this example repo)
# You can find them here: https://www.sqlite.org/download.html
add_library (sqlite3 STATIC
    "sqlite3.h"
    "sqlite3.c")
target_compile_definitions (sqlite3 PRIVATE SQLITE_THREADSAFE=0)
target_include_directories (sqlite3 PUBLIC ${PROJECT_SOURCE_DIR})

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
