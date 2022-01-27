
include(${CMAKE_CURRENT_SOURCE_DIR}/../../max-sdk-base/script/max-pretarget.cmake)

set(LIBS_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../libs")
set(AUBIO_PATH "${LIBS_PATH}/aubio/build/dist")


include_directories( 
	"${C74_INCLUDES}"
	"${AUBIO_PATH}/include"
)


add_library( 
	${PROJECT_NAME} 
	MODULE
	${PROJECT_NAME}.c
)

if(APPLE)
	target_link_libraries(${PROJECT_NAME} PUBLIC "-framework Accelerate")
	target_link_libraries(${PROJECT_NAME} PUBLIC ${AUBIO_PATH}/lib/libaubio.a)
else()
	#target_link_libraries(${PROJECT_NAME} PUBLIC ${LIBSR_PATH}/build/src/Release/samplerate.lib)
endif()

include(${CMAKE_CURRENT_SOURCE_DIR}/../../max-sdk-base/script/max-posttarget.cmake)
