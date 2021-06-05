#!/bin/bash

function build
{
    JOBS=`getconf _NPROCESSORS_ONLN`
    mkdir build
    pushd build
    cmake ../
#
#        -DLLVM_DIR=/opt/toolchain/9.0.1 \
#        -DLLVM_ROOT=/opt/toolchain/9.0.1 \
#        -DCMAKE_BUILD_TYPE=Debug \
    make -j$JOBS
    popd
}

codedir=(
gatlin
include
#pex
)

formatdir=(
gatlin
include
#pex
)

scope_file=".scopefile"
tag_file="tags"

function gen_scope
{
    > ${scope_file}
    for d in ${codedir[@]}; do
        find $d -type f \
            -a \( -name "*.h" -o -name "*.hpp" -o -name "*.cpp" -o -name "*.c" \
            -o -name "*.cc" \) >> ${scope_file}
    done
    rm -f scope.* ${tag_file}
    ctags -I "__THROW __nonnull __attribute_pure__ __attribute__ G_GNUC_PRINTF+" \
    --file-scope=yes --c++-kinds=+px --c-kinds=+px --fields=+iaS -Ra --extra=+fq \
    --langmap=c:.c.h.pc.ec --languages=c,c++ --links=yes \
    -f ${tag_file} -L ${scope_file}
    cscope -Rb -i ${scope_file}
}

function indent
{
    for d in ${formatdir[@]}; do
	    clang-format -i -style=llvm `find $d -name '*.cpp' -or -name "*.h"`
    done
}

case $1 in
    "tag")
        gen_scope
        ;;
    "build")
        build
        ;;
    "clean")
        rm -rf build
        rm -f ${tag_file} ${scope_file} cscope.out
        ;;
    "indent")
        indent
        ;;
    *)
        echo ./build.sh tag build clean indent
        ;;
esac

