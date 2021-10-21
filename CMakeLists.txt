cmake_minimum_required (VERSION 3.5)

file(GLOB_RECURSE SOURCE_FILES src/*.cpp src/*.c)
file(GLOB_RECURSE HEADER_FILES include/*.h)

idf_component_register(SRCS ${SOURCE_FILES}
                       INCLUDE_DIRS "include")

