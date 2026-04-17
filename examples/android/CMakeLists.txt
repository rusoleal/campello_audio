# Android NativeActivity example
# Builds a shared library that is loaded by android.app.NativeActivity.

if(NOT DEFINED ANDROID_NDK AND NOT DEFINED ENV{ANDROID_NDK})
    message(FATAL_ERROR "ANDROID_NDK not set. Required for Android example.")
endif()

set(ANDROID_NDK_PATH ${ANDROID_NDK})
if(NOT ANDROID_NDK_PATH)
    set(ANDROID_NDK_PATH $ENV{ANDROID_NDK})
endif()

set(NATIVE_APP_GLUE_DIR ${ANDROID_NDK_PATH}/sources/android/native_app_glue)
if(NOT EXISTS ${NATIVE_APP_GLUE_DIR}/android_native_app_glue.c)
    message(FATAL_ERROR "native_app_glue not found at ${NATIVE_APP_GLUE_DIR}")
endif()

add_library(native_app_glue STATIC ${NATIVE_APP_GLUE_DIR}/android_native_app_glue.c)
target_include_directories(native_app_glue PUBLIC ${NATIVE_APP_GLUE_DIR})

add_library(campello_audio_android_sampler SHARED main.cpp)

target_link_libraries(campello_audio_android_sampler PRIVATE
    campello_audio
    native_app_glue
    android
    log
)

target_link_options(campello_audio_android_sampler PRIVATE -u ANativeActivity_onCreate)
