# Static Analysis Tool of ACHyb

## Intro

In static analysis, ACHyb firstly performs an interface analysis to identify the permission checks. It then conducts a callsite dependence analysis to identify the privileged functions. Finally, it performs a constraint-based invariant analysis to produce the potentially vulnerable paths.

## Setup

ACHyb is built on top of the `PeX` tool. Therefore, its setup procedures are similar with `PeX's`.    

### LLVM

ACHyb requires the `LLVM-9` toolchain (including `clang` and `opt`). Its pre-built binaries are available at https://releases.llvm.org/download.html#9.0.0. Please add the path of the `bin` folder to the `PATH` variable.

```
export PATH=/path/to/llvm/bin:$PATH
```

### WLLVM

The [WLLVM](https://github.com/travitch/whole-program-llvm) project provides tools for building a whole-kernel LLVM bitcode file, which serves as the input of our static analysis tool. To install WLLVM, please install `Python3` and `pip` first, and then run the following command:

```bash
pip install wllvm
```

### Kernel Compilation

To generate the LLVM bitcode file for a Linux kernel,  just run the following commands to download and compile kernel source:

```bash
# download kernel code
git clone https://github.com/githubhuyang/linux.git
cd linux

# install dependencies
sudo apt install make flex bison libelf-dev libssl-dev

# compile kernel source using wllvm
export LLVM_COMPILER=clang
export JOBS=8 # the number of jobs processed in parallel
make ARCH=x86_64 CC=wllvm defconfig
make ARCH=x86_64 CC=wllvm -j$JOBS

# extract the bitcode file (vmlinux.bc)
extract-bc vmlinux
```
To reproduce our results, you may use `myallyes_defconfig` instead of `defconfig`. However, `defconfig` may significantly reduce the time cost of kernel compilation.

### Tool Compilation

To compile our tool, please install `cmake` first and then run the following command.

```bash
./build.sh build
```

As a result, you can get `build/achyb/libachyb.so`.

## Usage

To reproduce our results, just execute the `run.sh` script. 

```bash
./run.sh
```

It will generate three log files `cap.log`, `lsm.log` and `dac.log`, which save the results of Capabilities (CAP), Linux Security Module (LSM) and Discretionary Access Control (DAC), respectively.  Each log file is organized as follows:

```
Permission Check Detection:
... [a list of permission checks]
Time Cost: XXX s
Done

Privileged Function Detection:
... [a list of privileged functions]
Time Cost: XXX s
Done

Constraint-based Analysis:
... [a list of potentially vulnerable paths]
Time Cost: XXX s
Done
```

Each potentially vulnerable path is reported in the `func1:func2:func3` format.  `func2` is a privileged function called by both `func1` and `func3`;  In `func1`, a permission check is missing or misused, thus the callsite of`func2` is not protected;  In `func3`, the callsite of `func2` is protected by permission checks.

If you want to run PeX's invariant analysis with the permission checks and privileged functions detected by ACHyb, please run the following script:

```bash
./run-achyb-pex.sh
```

It will generate three log files as well. The format is similar with the format mentioned above except that the PeX invariant analysis uses the PeX-style format, shown as follows.

```
...

PeX Invariant Analysis:
... [logs reported by PeX's invariant analysis]
Time Cost: XXX s
Done
```
Note that it could take a few hours to generate the full results for the kernel compiled with `myallyes_defconfig`. We put the pre-generated results of ACHyb in the `results` folder for your reference.
