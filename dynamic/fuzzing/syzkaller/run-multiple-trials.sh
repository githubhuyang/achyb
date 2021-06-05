#!/bin/bash

# $1: the path of config file
# $2: the number of trails
# $3: the port number

if [ "$#" -lt 3 ]; then
    echo "Illegal number of parameters"
    exit 1
fi

if [ ! -f "$1" ]; then
    echo "No such file in $1"
    exit 1
fi

BASE_CFG_FILE=$(basename "$1")

# use soft link instead of copy
#if [ -f "$BASE_CFG_FILE" ]; then
#    rm $BASE_CFG_FILE
#fi
#ln -s "$1" "$BASE_CFG_FILE"

IFS=\. read -a fields <<< "$BASE_CFG_FILE"
HEURISTIC=${fields[0]}
NUM_TRIALS=$2
CUR_WORKDIR=$(cat $1 | grep '"workdir"' | grep -o -e '"\S*",')
CUR_SERVER=$(cat $1 | grep '"http"' | grep -o -e '"\S*",')
CUR_COUNT=$(cat $1 | grep '"count"' | grep -o -e '\S*,')

for ii in $(seq 1 $NUM_TRIALS)
do
    TRIAL_NAME=$HEURISTIC\_trial\_$ii
    echo $TRIAL_NAME

    BIN_DIR=$TRIAL_NAME\_bin
    rm -rf $BIN_DIR
    cp -r bin/ $BIN_DIR

    TRIAL_WORKDIR=$TRIAL_NAME\_workdir
    TRIAL_CFG_FILE=$TRIAL_WORKDIR/$TRIAL_NAME.cfg
    TRIAL_PORT=$(expr $3 + $ii)

    rm -rf $TRIAL_WORKDIR
    mkdir -p $TRIAL_WORKDIR
  
    cp $1 $TRIAL_CFG_FILE

    cp -r seeds $TRIAL_WORKDIR

    # Replace workdir path
    sed -i "s#$CUR_WORKDIR#\"$(realpath $TRIAL_WORKDIR)\",#g" $TRIAL_CFG_FILE

    # Replace server
    sed -i "s#$CUR_SERVER#\"127.0.0.1:$TRIAL_PORT\",#g" $TRIAL_CFG_FILE

    # Replace VM count with 1
    # sed -i "s#\"count\": $CUR_COUNT#\"count\": 1,#g" $TRIAL_CFG_FILE

    # Replace bindir with copied directory
    sed -i "s#\"bindir\": \"bin\",#\"bindir\": \"$BIN_DIR\",#g" $TRIAL_CFG_FILE

    # Start trial in detached, named tmux session
    tmux new-session -d -s syzkaller_$TRIAL_NAME
    tmux send-keys -t syzkaller_$TRIAL_NAME:0 './syz-manager-with-plot.sh '"$TRIAL_NAME"' '"$BIN_DIR"'; push "Trial '"$ii"' stopped";'
    tmux send-keys -t syzkaller_$TRIAL_NAME:0 Enter
done


