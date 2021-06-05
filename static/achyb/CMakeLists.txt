if (${LLVM_PACKAGE_VERSION} VERSION_LESS "8.0")
    add_llvm_loadable_module( libachyb
        utility.cpp
        simple_set.cpp
        gating_function.cpp
        module_duplicator.cpp
        cvfa.cpp
        perm_check.cpp
        achyb.cpp

        DEPENDS
        PLUGIN_TOOL
        opt
        )
else()
    add_llvm_library( libachyb MODULE
        utility.cpp
        simple_set.cpp
        gating_function.cpp
        module_duplicator.cpp
        cvfa.cpp
        perm_check.cpp
        achyb.cpp

        DEPENDS
        PLUGIN_TOOL
        opt
        )
endif()

target_compile_features(libachyb PRIVATE cxx_range_for cxx_auto_type)

if ( CMAKE_SYSTEM_NAME MATCHES "Darwin")
    llvm_map_components_to_libnames(llvm_libs bitwriter
        core ipo irreader instcombine instrumentation target linker analysis
        scalaropts support )
    target_link_libraries( libachyb LLVMSvf LLVMCudd ${llvm_libs})
else()
    target_link_libraries( libachyb LLVMSvf LLVMCudd )
endif()

