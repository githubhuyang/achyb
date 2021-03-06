# Fuzzing for ACHyb

Our dynamic analysis tool performs fuzzing to verify the potentially vulnerable paths reported by our static analysis tool.

## Setup

### Tool Compilation

Our tool is built on top of the `syzkaller` fuzzer. Basically, you need to 1) install the `Go` toolchain first, 2) download the official `syzkaller` version (saved in `gopath/src/github.com/google/syzkaller`) and rename the `syzkaller` folder to `official`, 3) put the `dynamic/fuzzing/syzkaller` folder into the `gopath/src/github.com/google` folder, and 4) use `make` to build our `syzkaller`  version. Here, we provide the script for the above four steps. Please refer to [Syzkaller's document]() for more setup details.

```bash
# go toolchain setup
wget https://dl.google.com/go/go1.14.2.linux-amd64.tar.gz
tar -xf go1.14.2.linux-amd64.tar.gz
mv go goroot
mkdir gopath
export GOPATH=`pwd`/gopath
export GOROOT=`pwd`/goroot
export PATH=$GOPATH/bin:$PATH
export PATH=$GOROOT/bin:$PATH

# download official syzkaller version and rename folder name
go get -u -d github.com/google/syzkaller/prog
mv gopath/src/github.com/google/syzkaller gopath/src/github.com/google/official

# download our syzkaller version and move to gopath/src/github.com/google folder
git clone https://github.com/githubhuyang/achyb.git
cp -r achyb/dynamic/fuzzing/syzkaller gopath/src/github.com/google

# build source code of our syzkaller version
cd gopath/src/github.com/google/syzkaller
make
```

As a result, you will get the `bin` folder which stores executable files for our `syzkaller` version.

### Python Execution Environment

Our tool includes a `Python3` script for visualizing the fuzzing progress. We suggest using the [Anaconda 3](https://www.anaconda.com/products/individual) package distribution, which includes common software packages for figure plot such as `matplotlib`.

### Invariant Check Injection

You need to manually inject invariant checks to the kernel source based on the potentially vulnerable paths reported by our static analysis tool. Here, we provide an invariant check template for your reference.

```c
...
bool ac = perm_check();
// if no perm_check was called, just set ac to 0
// i.e., bool ac = 0;
...
privileged_function();

// Invariant check
if(!ac) {
	printk("BUG: achyb function_name\n");
}
...
```

As can be seen, the essence of the above template is to print a message when the access control decision is denied. The message can be detected and collected by our fuzzer.

In future, we will upgrade our tool to support fully automated invariant check injection. 

### Kernel Image

After the check injection, you need to compile the modified kernel source to generate a kernel image. Here, we provide a demo script for kernel source compilation. 

```bash
git clone https://github.com/githubhuyang/linux.git
...
# compile kernel source after injecting invariant checks
make ARCH=x86_64 my_x86_64_defconfig
make ARCH=x86_64 -j$JOBS
```

### QEMU and QEMU Image

QEMU is required to load the kernel image and execute test cases generated by our fuzzer. One simple way to install QEMU for x86 platform is to run the following command:

```bash
sudo apt install qemu-system-x86
```
An alternative method is to follow the [QEMU's instructions](https://www.qemu.org/) to compile QEMU source code.

Besides, please follow the [Syzkaller's instructions](https://github.com/google/syzkaller/blob/master/docs/linux/setup_ubuntu-host_qemu-vm_x86-64-kernel.md#image) to create a QEMU image.
```bash
sudo apt install debootstrap

mkdir qemu_image
cd qemu_image
wget https://raw.githubusercontent.com/google/syzkaller/master/tools/create-image.sh -O create-image.sh
chmod +x create-image.sh
./create-image.sh
```

### Seed Programs

Our tool takes a set of distilled seed programs as inputs, which can guide the fuzzer to rapidly approach targets with less chance of being trapped. To load the distilled seed programs, just put the `distill.db` generated by our seed distillation tool in the `seeds` folder, and declare the db file in the fuzzing configuration file (more details about the configuration file will be introduced below). 

### Fuzzing Configuration

Before running our `syzkaller` version, you need to write a configuration file. We provide a configuration template which is saved in `syzkaller/config/achyb.cfg`. Please change all file paths to the correct ones in your machine, and use the `corpus_db_files`keyword to select the db file in the `seeds` folder that you are going to use. Besides using our template, you may follow the [Syzkaller's instructions](https://github.com/google/syzkaller/blob/master/docs/configuration.md) to write your own configuration file.

### Other Required Tools

The `tmux` tool is used to create multiple fuzzing trails. To install `tmux`, just run the following command:

```bash
sudo apt install tmux
```

## Usage

Just run the following script to start the fuzzing campaign:

```bash
./run.sh
```

The fuzzing progress will be saved in the `achyb_trial_1_workdir/fuzz_progress.png` and `achyb_trial_1_workdir/fuzz_progress.csv`. The detected vulnerabilities will be saved in the `achyb_trial_1_workdir/crashes` folder.

You may modify `run.sh` according to your requirements.  For example, you may set the `CONFIG_PATH` variable to a new configuration file. You may set more trails by increasing the `NUM_TRAILS` variable.
