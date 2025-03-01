#!/bin/bash
set -x 

export PREFIX=$1

script_name=$0
script_full_path=$(dirname "$0")
cd $script_full_path
pwd

# stop all children when script is terminated
trap "trap - SIGTERM && kill -- -$$" SIGINT SIGTERM EXIT

./benchbench.sh /blk/intel0	i0-$PREFIX 4K 512K 28500 &
sleep 10s
./benchbench.sh /blk/k4		k4-$PREFIX 4K 512K 77500 &
sleep 10s
./benchbench.sh /blk/wd		wd-$PREFIX 4K 512K 12250 &
sleep 10s
./benchbench.sh /blk/s0		s0-$PREFIX 4K 512K 33750 &
sleep 10s
./benchbench.sh /blk/h0		h0-$PREFIX 4K 512K 17500 &
sleep 10s

if false; then
./benchbench.sh /blk/m0		m0-$PREFIX 4K 512K 21250 &
fi

sleep 10s
./benchbench.sh /blk/mp0		mp0-$PREFIX 4K 512K 36250 &



wait $(jobs -p)
