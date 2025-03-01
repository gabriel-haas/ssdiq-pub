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

#SHUFFLE="-shuffle"

OVERWRITE=3
#sudo -E FILENAME=$FILENAME INIT=disable IO_SIZE_PERCENT_FILE=1 IO_DEPTH=1   BS=$SEQ_BS THREADS=1 PATTERN=sequential RW=1 iob/iob >> iob-output-$PREFIX.csv
sanitize_nvme_and_init
sudo -E FILENAME=$FILENAME INIT=false   IO_SIZE_PERCENT_FILE=$OVERWRITE IO_DEPTH=128 BS=$UNI_BS THREADS=1 PATTERN=zones-noshuffle ZONES="s1 f1 sequential s1 f1.2 sequential s1 f1.4 sequential s1 f1.6 sequential s1 f1.8 sequential s1 f2.0 sequential s1 f2.2 sequential s1 f2.4 sequential s1 f2.6 sequential s1 f2.8 sequential s1 f3.0 sequential s1 f3.2 sequential s1 f3.4 sequential s1 f3.6 sequential s1 f3.8 sequential s1 f4.0 sequential s1 f4.2 sequential s1 f4.4 sequential s1 f4.6 sequential s1 f4.8 sequential s1 f5.0 sequential s1 f5.2 sequential s1 f5.4 sequential s1 f5.6 sequential s1 f5.8 sequential s1 f6.0 sequential s1 f6.2 sequential s1 f6.4 sequential s1 f4.6 sequential s1 f6.8 sequential s1 f7.0 sequential s1 f7.8 sequential " RATE=0 RW=1 iob/iob >> iob-output-$PREFIX.csv 
sanitize_nvme_and_init
sudo -E FILENAME=$FILENAME INIT=false   IO_SIZE_PERCENT_FILE=$OVERWRITE IO_DEPTH=128 BS=$UNI_BS THREADS=1 PATTERN=zones-noshuffle ZONES="s1 f1 sequential s1 f1.2 sequential s1 f1.4 sequential s1 f1.6 sequential s1 f1.8 sequential s1 f2.0 sequential s1 f2.2 sequential s1 f2.4 sequential s1 f2.6 sequential s1 f2.8 sequential s1 f3.0 sequential s1 f3.2 sequential s1 f3.4 sequential s1 f3.6 sequential s1 f3.8 sequential s1 f4.0 sequential" RATE=0 RW=1 iob/iob >> iob-output-$PREFIX.csv 
sanitize_nvme_and_init
sudo -E FILENAME=$FILENAME INIT=false   IO_SIZE_PERCENT_FILE=$OVERWRITE IO_DEPTH=128 BS=$UNI_BS THREADS=1 PATTERN=zones-noshuffle ZONES="s1 f1 sequential s1 f1.2 sequential s1 f1.4 sequential s1 f1.6 sequential s1 f1.8 sequential s1 f2.0 sequential s1 f2.2 sequential s1 f2.4 sequential" RATE=0 RW=1 iob/iob >> iob-output-$PREFIX.csv 
sanitize_nvme_and_init
sudo -E FILENAME=$FILENAME  INIT=false   IO_SIZE_PERCENT_FILE=$OVERWRITE IO_DEPTH=128 BS=$UNI_BS THREADS=1 PATTERN=zones-noshuffle ZONES="s1 f1 sequential s1 f1.2 sequential s1 f1.4 sequential s1 f1.6 sequential" RATE=0 RW=1 iob/iob >> iob-output-$PREFIX.csv 
sanitize_nvme_and_init
sudo -E FILENAME=$FILENAME INIT=false   IO_SIZE_PERCENT_FILE=$OVERWRITE IO_DEPTH=128 BS=$UNI_BS THREADS=1 PATTERN=zones-noshuffle ZONES="s1 f1 sequential s1 f1.2 sequential" RATE=0 RW=1 iob/iob >> iob-output-$PREFIX.csv 
sanitize_nvme_and_init
sudo -E FILENAME=$FILENAME INIT=false   IO_SIZE_PERCENT_FILE=$OVERWRITE IO_DEPTH=128 BS=$UNI_BS THREADS=1 PATTERN=uniform RATE=0 RW=1 iob/iob >> iob-output-$PREFIX.csv 


# fourzones
#sudo -E INIT=false   IO_SIZE_PERCENT_FILE=$OVERWRITE IO_DEPTH=128 BS=$UNI_BS THREADS=1 PATTERN=zones-noshuffle ZONES="s1 f1 sequential s1 f2 sequential s1 f3 sequential s1 f4 sequential" RATE=0 RW=1 iob/iob >> iob-output-$PREFIX.csv 
#sudo -E INIT=false   IO_SIZE_PERCENT_FILE=$OVERWRITE IO_DEPTH=128 BS=$UNI_BS THREADS=1 PATTERN=zones-noshuffle ZONES="s0.9 f0.5 sequential s0.1 f0.5 sequential" RATE=0 RW=1 iob/iob >> iob-output-$PREFIX.csv
#sudo -E INIT=false   IO_SIZE_PERCENT_FILE=$OVERWRITE IO_DEPTH=128 BS=$UNI_BS THREADS=1 PATTERN=zones-noshuffle ZONES="s0.9 f0.5 s0.1 f0.5" RATE=0 RW=1 iob/iob >> iob-output-$PREFIX.csv

#sudo -E INIT=false   IO_SIZE_PERCENT_FILE=$OVERWRITE IO_DEPTH=128 BS=4K THREADS=1 PATTERN=sequential RATE=300000 RW=1 iob/iob >> iob-output-$PREFIX.csv
#sudo -E INIT=false   IO_SIZE_PERCENT_FILE=$OVERWRITE IO_DEPTH=128 BS=$UNI_BS THREADS=1 PATTERN=zipf ZIPF=0.6 RATE=10000 RW=0.99 iob/iob >> iob-output-$PREFIX.csv



