#!/bin/bash
set -x 

export FILENAME=$1
export FILESIZE= # let iob figure it out
export PREFIX=$2
UNI_BS=${3:-4K}
SEQ_BS=${4:-512K}

export IOENGINE=io_uring 
export FILL=1

cmake -DCMAKE_BUILD_TYPE=Release ..
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
	sudo -E INIT=disable IO_SIZE_PERCENT_FILE=1 IO_DEPTH=1   BS=$SEQ_BS THREADS=1 PATTERN=sequential-noshuffle RW=1 iob/iob >> iob-output-$PREFIX.csv
}
run_wa_exp_zipf() {
	ZIPF=$1
	sudo -E INIT=disable IO_SIZE_PERCENT_FILE=$OVERWRITE  IO_DEPTH=256 BS=$UNI_BS THREADS=4 PATTERN=zipf$SHUFFLE ZIPF=$ZIPF RW=1  iob/iob >> iob-output-$PREFIX.csv
	sleep 2m
}
run_wa_exp_fiozipf() {
	ZIPF=$1
	sudo -E INIT=disable IO_SIZE_PERCENT_FILE=$OVERWRITE  IO_DEPTH=256 BS=$UNI_BS THREADS=1 PATTERN=fiozipf$SHUFFLE ZIPF=$ZIPF RW=1  iob/iob >> iob-output-$PREFIX.csv
	sleep 2m
}
run_wa_exp_uniform() {
	sudo -E INIT=disable   IO_SIZE_PERCENT_FILE=$OVERWRITE IO_DEPTH=256 BS=$UNI_BS THREADS=4 PATTERN=uniform	RW=1 iob/iob >> iob-output-$PREFIX.csv
	sleep 2m
}
run_wa_exp_zones() {
	ZONES=$1
	sudo -E INIT=disable IO_SIZE_PERCENT_FILE=$OVERWRITE  IO_DEPTH=256 BS=$UNI_BS THREADS=1 PATTERN=zones$SHUFFLE ZONES=$ZONES RW=1  iob/iob >> iob-output-$PREFIX.csv
	sleep 2m
}

SHUFFLE="-shuffle"

PREFIXBASE=$PREFIX

if false; then

# fiozipf
OVERWRITE=10
# sanitize / uniform
export PREFIX="$PREFIXBASE-san-none"
sanitize_nvme
run_wa_exp_fiozipf 0.8
OVERWRITE=10
# sanitize / seq / uniform
export PREFIX="$PREFIXBASE-san-seq"
sanitize_nvme_and_init
run_wa_exp_fiozipf 0.8

fi

# Zipf shuflle
SHUFFLE="-shuffle"
OVERWRITE=8
# sanitize / uniform
export PREFIX="$PREFIXBASE-san-none"
sanitize_nvme
run_wa_exp_zipf 0.8
OVERWRITE=4
# sanitize / seq / uniform
export PREFIX="$PREFIXBASE-san-seq"
sanitize_nvme_and_init
run_wa_exp_zipf 0.8

# Zipf NO-shuflle
SHUFFLE="-noshuffle"
OVERWRITE=8
# sanitize / uniform
export PREFIX="$PREFIXBASE-san-none"
sanitize_nvme
run_wa_exp_zipf 0.8
OVERWRITE=4
# sanitize / seq / uniform
export PREFIX="$PREFIXBASE-san-seq"
sanitize_nvme_and_init
run_wa_exp_zipf 0.8

## uniform
OVERWRITE=8
# sanitize / uniform
export PREFIX="$PREFIXBASE-san-none"
sanitize_nvme
run_wa_exp_uniform
OVERWRITE=4
# sanitize / seq / uniform
export PREFIX="$PREFIXBASE-san-seq"
sanitize_nvme_and_init
run_wa_exp_uniform



