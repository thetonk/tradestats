add_library(myLibs STATIC cJSON.c vector.c utilities.c queue.c)
target_include_directories(myLibs PUBLIC "${PROJECT_SOURCE_DIR}/include")
