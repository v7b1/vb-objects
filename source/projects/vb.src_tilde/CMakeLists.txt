
include(${CMAKE_CURRENT_SOURCE_DIR}/../../max-sdk-base/script/max-pretarget.cmake)

# set(LIBSR_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../libs/libsamplerate/build/src")
# set(LIBSRI_PATH "${CMAKE_CURRENT_SOURCE_DIR}/libsamplerate/include")

set(LIBSR_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../libs/libsamplerate")


include_directories( 
	"${C74_INCLUDES}"
	"${LIBSR_PATH}/include"
)


add_library( 
	${PROJECT_NAME} 
	MODULE
	${PROJECT_NAME}.c
)

if(APPLE)
	target_link_libraries(${PROJECT_NAME} PUBLIC "-framework Accelerate")
	target_link_libraries(${PROJECT_NAME} PUBLIC ${LIBSR_PATH}/build/src/libsamplerate.a)
else()
	target_link_libraries(${PROJECT_NAME} PUBLIC ${LIBSR_PATH}/build/src/Release/samplerate.lib)
endif()

include(${CMAKE_CURRENT_SOURCE_DIR}/../../max-sdk-base/script/max-posttarget.cmake)
