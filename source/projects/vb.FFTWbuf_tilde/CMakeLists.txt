
include(${CMAKE_CURRENT_SOURCE_DIR}/../../max-sdk-base/script/max-pretarget.cmake)

set(LIBS_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../libs")


include_directories( 
	"${C74_INCLUDES}"
	"${LIBS_PATH}/fftw/include"
)


add_library( 
	${PROJECT_NAME} 
	MODULE
	${PROJECT_NAME}.c
)

if(APPLE)
	target_link_libraries(${PROJECT_NAME} PUBLIC ${LIBS_PATH}/fftw/libfftw3f.a)
else()
	#target_link_libraries(${PROJECT_NAME} PUBLIC ${LIBSR_PATH}/build/src/Release/samplerate.lib)
endif()

include(${CMAKE_CURRENT_SOURCE_DIR}/../../max-sdk-base/script/max-posttarget.cmake)
