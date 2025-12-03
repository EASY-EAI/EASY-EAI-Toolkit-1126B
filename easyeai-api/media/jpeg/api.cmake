find_package(PkgConfig)
pkg_search_module(GST1.0 REQUIRED gstreamer-1.0)
pkg_search_module(GST1.0_APP REQUIRED gstreamer-app-1.0)

# source code path
file(GLOB JPEG_SOURCE_DIRS
    ${CMAKE_CURRENT_LIST_DIR}/*.c 
    ${CMAKE_CURRENT_LIST_DIR}/*.cpp 
    )

# headfile path
set(JPEG_INCLUDE_DIRS 
    ${CMAKE_CURRENT_LIST_DIR} 
    ${GST1.0_INCLUDE_DIRS} 
    ${GST1.0_APP_INCLUDE_DIRS} 
    )

# c/c++ flags
set(JPEG_LIBS 
    ${GST1.0_LIBRARIES} 
    ${GST1.0_APP_LIBRARIES} 
    pthread 
    )
