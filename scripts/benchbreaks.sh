#!/bin/bash
set -x 

export FILENAME=$1
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
run_wa_exp_zipf() {
	ZIPF=$1
	sudo -E  FILENAME=$FILENAME INIT=false IO_SIZE_PERCENT_FILE=$OVERWRITE  IO_DEPTH=128 BS=$UNI_BS THREADS=4 PATTERN=zipf$SHUFFLE ZIPF=$ZIPF RW=1  iob/iob >> iob-output-$PREFIX.csv
	sleep 2m
}
run_wa_exp_uniform() {
	sudo -E  FILENAME=$FILENAME INIT=false   IO_SIZE_PERCENT_FILE=$OVERWRITE IO_DEPTH=128 BS=$UNI_BS THREADS=4 PATTERN=uniform	RW=1 iob/iob >> iob-output-$PREFIX.csv
	sleep 2m
}
run_wa_exp_zones() {
	ZONES=$1
	sudo -E  FILENAME=$FILENAME INIT=false IO_SIZE_PERCENT_FILE=$OVERWRITE  IO_DEPTH=128 BS=$UNI_BS THREADS=4 PATTERN=zones$SHUFFLE ZONES=$ZONES RW=1  iob/iob >> iob-output-$PREFIX.csv
	sleep 2m
}


OVERWRITE=5
#sanitize_nvme_and_init
sudo -E FILENAME=$FILENAME INIT=false   IO_SIZE_PERCENT_FILE=$OVERWRITE IO_DEPTH=256 BS=$UNI_BS THREADS=4 PATTERN=uniform RATE=0 RW=1 BREAK_EVERY=3600 BREAK_FOR=1800 iob/iob >> iob-output-$PREFIX.csv 


