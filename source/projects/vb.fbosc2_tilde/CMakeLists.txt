
include(${CMAKE_CURRENT_SOURCE_DIR}/../../max-sdk-base/script/max-pretarget.cmake)


include_directories( 
	"${C74_INCLUDES}"
)


add_library( 
	${PROJECT_NAME} 
	MODULE
	${PROJECT_NAME}.c
)

if(APPLE)
	target_link_libraries(${PROJECT_NAME} PUBLIC "-framework Accelerate")
endif()

include(${CMAKE_CURRENT_SOURCE_DIR}/../../max-sdk-base/script/max-posttarget.cmake)
