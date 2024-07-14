add_library(myLibs cJSON.c vector.c)
target_include_directories(myLibs PRIVATE "${PROJECT_SOURCE_DIR}/include")
