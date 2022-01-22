
include(${CMAKE_CURRENT_SOURCE_DIR}/../../max-sdk-base/script/max-pretarget.cmake)


include_directories( 
	"${C74_INCLUDES}"
)


add_library( 
	${PROJECT_NAME} 
	MODULE
	${PROJECT_NAME}.c
	mt19937-64.c
	mt64.h
)


include(${CMAKE_CURRENT_SOURCE_DIR}/../../max-sdk-base/script/max-posttarget.cmake)
