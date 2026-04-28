# Set library output naming schme based on value of BUILD_SHARED_LIBS option

if(DEFINED BUILD_SHARED_LIBS)
	set_target_properties(${PROJECT_LIB_NAME} PROPERTIES
		OUTPUT_NAME "${PROJECT_LIB_NAME}"
		PREFIX ""
		SUFFIX ".so"
	)
else()
	set_target_properties(${PROJECT_LIB_NAME} PROPERTIES
		OUTPUT_NAME "${PROJECT_LIB_NAME}"
		PREFIX ""
		SUFFIX ".a"
	)
endif()