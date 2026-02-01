##ÔÚ/usr/share/cmake-3.18/Modules/Ä¿Â¼ÏÂµÄFind*.cmake£¬¶¼ÄÜÍ¨¹ýÕâÖÖ·½Ê½±»ÕÒµ½
##¾ßÌåµÄ±äÁ¿(Èç${OpenCV_INCLUDE_DIRS}¡¢${OpenCV_LIBS})»á±»¶¨ÒåÔÚÀïÃæµÄÏà¶ÔÓ¦µÄFind*.cmakeÎÄ¼þÖÐ(Í¨³£¾ÍÐ´ÔÚ¿ªÍ·µÄÃèÊöÀï)
#find_package(OpenCV REQUIRED)
#
set(OpenCV_INCLUDE_DIRS
    ${CMAKE_SYSROOT}/usr/include/ 
    ${CMAKE_SYSROOT}/usr/include/opencv4/ 
)
set(OpenCV_LIBS_DIRS
    ${CAMKE_SYSROOT}/usr/lib/aarch64-linux-gnu/lapack
    ${CAMKE_SYSROOT}/usr/lib/aarch64-linux-gnu/blas
)
set(OpenCV_LIBS
    opencv_core 
    opencv_imgproc 
    opencv_imgcodecs
#    opencv_calib3d 
#    opencv_dnn 
#    opencv_features2d 
#    opencv_flann 
#    opencv_highgui 
#    opencv_ml 
#    opencv_objdetect 
#    opencv_photo 
#    opencv_stitching
#    opencv_videoio 
#    opencv_video  
)

# static Library paths
set(GESTURES_LIBS_DIRS
    ${CMAKE_CURRENT_LIST_DIR}
    ${OpenCV_LIBS_DIRS}
    )

# headfile path
set(GESTURES_INCLUDE_DIRS
    ${OpenCV_INCLUDE_DIRS} 
    ${CMAKE_CURRENT_LIST_DIR} 
    )

# c/c++ flags
set(GESTURES_LIBS 
    gestures
    rknnrt
    ${OpenCV_LIBS} 
    pthread 
    stdc++ 
    )
