#!/bin/bash

# Make FIFO for IPC
FIFO_NAME=$1.fifo
BIN_DIR=$2
rm -f $FIFO_NAME
mkfifo $1\_workdir/$FIFO_NAME

# Start the Python script
python plot_cov_time.py $1\_workdir $1\_workdir/$FIFO_NAME &

# Start Syzkaller # -debug
$BIN_DIR/syz-manager -config=$1\_workdir/$1\.cfg 2>&1 | tee $1\_workdir/$FIFO_NAME | tee $1\_workdir/$1.log

