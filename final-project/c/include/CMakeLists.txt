add_library(myLibs STATIC cJSON.c vector.c)
target_include_directories(myLibs PUBLIC "${PROJECT_SOURCE_DIR}/include")
