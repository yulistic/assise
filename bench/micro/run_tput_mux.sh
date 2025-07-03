#!/bin/bash
set -ex

## This script assumes tmux sessions are created with bench/micro/setup_tmux.sh
PROJ_ROOT="/home/jongyul/assise"
OP="sw sr rr rw"
FILE_SIZE="10G"
IO_SIZE="4K"
THREAD_NUM="1"
OUT_DIR="results"

mkfs() {
	tmux send-keys -t 0 "cd $PROJ_ROOT/kernfs/tests && sudo ./mkfs.sh" Enter
	sleep 3
}

dropCache() {
	sudo sh -c "echo 3 > /proc/sys/vm/drop_caches"
}

killKernfs() {
	sudo pkill -9 kernfs || true
	sleep 1
}

startKernfs() {
	tmux send-keys -t 0 "cd $PROJ_ROOT/kernfs/tests && sudo ./run.sh kernfs" Enter
	sleep 2
}

mkdir -p "$OUT_DIR"

for op in $OP; do

	killKernfs

	if [ "$op" == "sw" ] || [ "$op" == "rw" ]; then
		mkfs
	fi

	dropCache

	startKernfs

	cmd="sudo numactl -N1 -m1 ./run.sh iobench $op $FILE_SIZE $IO_SIZE $THREAD_NUM"
	echo $cmd
	script -c "$cmd" -f "$OUT_DIR/${op}_${FILE_SIZE}_${IO_SIZE}_${THREAD_NUM}.log"
done
