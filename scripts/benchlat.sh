#!/bin/bash
set -x 

export FILENAME=$1

#export FILESIZE=1899999789056 # AWS
#SEQ_BS=512K
#export FILESIZE=1920383057920 # INTEL
#SEQ_BS=128K
#export FILESIZE=3840755630080 # KIOXIA & SAMSUNG MILAN
export FILESIZE= # let iob figure it out
export PREFIX=$2
UNI_BS=${3:-4K}
SEQ_BS=${4:-512K}

export IOENGINE=io_uring 
export FILL=1

cmake -DCMAKE_BUILD_TYPE=Release ..
#cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo ..
#cmake -DCMAKE_BUILD_TYPE=Debug ..
make -j iob

sanitize_nvme() {
    echo "sanitize"
    sudo nvme sanitize --sanact=2 "$FILENAME"
    sleep 2m
    sudo nvme sanitize-log "$FILENAME"
    sleep 2m
    sudo blkdiscard  -v "$FILENAME"
    sleep 2m
}
sanitize_nvme_and_init() {
	sanitize_nvme
	sudo -E FILENAME=$FILENAME INIT=disable IO_SIZE_PERCENT_FILE=1 IO_DEPTH=1   BS=$SEQ_BS THREADS=1 PATTERN=sequential RW=1 iob/iob >> iob-output-$PREFIX.csv
}
run_lat_exp() {
	RATE=$1
	# debug changes runtime = 300/600 and sleep 5m
	sudo -E FILENAME=$FILENAME INIT=false IO_DEPTH=64 BS=$UNI_BS THREADS=7 PATTERN=$PATTERN$SHUFFLE RW=0.99 RATE=$RATE EXPRATE=$EXPRATE RUNTIME=130 LATENCYTHREAD=1 iob/iob >> iob-output-$PREFIX.csv
	sleep 30s
}

OVERWRITE=3
PATTERN=uniform
SHUFFLE="-shuffle"
EXPRATE=1

# INIT
if false; then
sanitize_nvme_and_init
# uniform
sudo -E FILENAME=$FILENAME INIT=false   IO_SIZE_PERCENT_FILE=$OVERWRITE IO_DEPTH=128 BS=$UNI_BS THREADS=4 PATTERN=uniform	RW=1 iob/iob >> iob-output-$PREFIX.csv
# slow init
sleep 5m
sudo -E FILENAME=$FILENAME INIT=false IO_DEPTH=64 BS=$UNI_BS THREADS=7 PATTERN=$PATTERN$SHUFFLE RW=0.99 RATE=25000 EXPRATE=$EXPRATE RUNTIME=300 LATENCYTHREAD=1 iob/iob >> iob-output-$PREFIX.csv
sleep 5m
fi
# INIT END

if [[ $FILENAME == /blk/k* ]]; then
run_lat_exp 500000
run_lat_exp 300000
run_lat_exp 250000
fi
if [[ $FILENAME == /blk/s* ]]; then
run_lat_exp 500000
run_lat_exp 300000
run_lat_exp 250000
fi
if false; then
run_lat_exp 200000
run_lat_exp 150000
run_lat_exp 130000
run_lat_exp 120000
run_lat_exp 110000
run_lat_exp 100000
run_lat_exp  90000
fi
run_lat_exp  80000
run_lat_exp  70000
run_lat_exp  60000
run_lat_exp  50000
run_lat_exp  40000
run_lat_exp  30000
run_lat_exp  20000
run_lat_exp  10000

exit 0

sanitize_nvme_and_init
# uniform
sudo -E FILENAME=$FILENAME INIT=false   IO_SIZE_PERCENT_FILE=$OVERWRITE IO_DEPTH=128 BS=$UNI_BS THREADS=4 PATTERN=uniform	RW=1 iob/iob >> iob-output-$PREFIX.csv
# slow init
sleep 5m
sudo -E FILENAME=$FILENAME INIT=false IO_DEPTH=64 BS=$UNI_BS THREADS=7 PATTERN=$PATTERN$SHUFFLE RW=0.99 RATE=25000 EXPRATE=$EXPRATE RUNTIME=300 LATENCYTHREAD=1 iob/iob >> iob-output-$PREFIX.csv
sleep 5m

run_lat_exp 100001
run_lat_exp  90001
run_lat_exp  80001
run_lat_exp  70001
run_lat_exp  60001
run_lat_exp  50001
run_lat_exp  40001
run_lat_exp  30001
run_lat_exp  20001
run_lat_exp  10001


run_lat_exp 100002
run_lat_exp  90002
run_lat_exp  80002
run_lat_exp  70002
run_lat_exp  60002
run_lat_exp  50002
run_lat_exp  40002
run_lat_exp  30002
run_lat_exp  20002
run_lat_exp  10002

