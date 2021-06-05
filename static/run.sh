opt-9 \
    -analyze \
    -load=build/achyb/libachyb.so \
    -achyb \
    -pclist=pc/cap.lst \
    linux/vmlinux.bc \
    -o /dev/null 2>&1 | tee cap.log

opt-9 \
    -analyze \
    -load=build/achyb/libachyb.so \
    -achyb \
    -pclist=pc/lsm.lst \
    linux/vmlinux.bc \
    -o /dev/null 2>&1 | tee lsm.log

opt-9 \
    -analyze \
    -load=build/achyb/libachyb.so \
    -achyb \
    -pclist=pc/dac.lst \
    linux/vmlinux.bc \
    -o /dev/null 2>&1 | tee dac.log