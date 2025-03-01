#!/bin/bash
set -x 

export PREFIX=$1

script_name=$0
script_full_path=$(dirname "$0")
cd $script_full_path
pwd

# stop all children when script is terminated
trap "trap - SIGTERM && kill -- -$$" SIGINT SIGTERM EXIT



./benchslow.sh /blk/k4		k4-$PREFIX 4K 512K &
sleep 20s
./benchslow.sh /blk/k5		k5-$PREFIX 4K 512K &
sleep 10s
./benchslow.sh /blk/wd		wd-$PREFIX 4K 512K &
sleep 10s
./benchslow.sh /blk/intel0	i0-$PREFIX 4K 512K &
sleep 10s
./benchslow.sh /blk/intel1	i1-$PREFIX 4K 512K &
sleep 10s
./benchslow.sh /blk/s0		s0-$PREFIX 4K 512K &
sleep 10s
./benchslow.sh /blk/h0		h0-$PREFIX 4K 512K &
sleep 10s
./benchslow.sh /blk/m0		m0-$PREFIX 4K 512K &
sleep 10s
./benchslow.sh /blk/mp0		mp0-$PREFIX 4K 512K &


wait $(jobs -p)
