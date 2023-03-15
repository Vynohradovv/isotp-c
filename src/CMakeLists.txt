set(APP_LIB_SOURCE
    isotp.c   
)

add_library(${APP_LIB_NAME} SHARED ${APP_LIB_SOURCE})
target_compile_features(${APP_LIB_NAME} PRIVATE ${CMAKE_C_COMPILE_FEATURES})