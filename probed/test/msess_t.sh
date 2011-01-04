#! /bin/sh
#
# Test of msess storage performance.
#

for n_sess in 8 16 32 64 128 256 512 1024; do
	for n_probes in 8 16 32 64 128 256 512 1024; do
		echo "Running $n_sess x $n_probes"
		./msess_t $n_sess $n_probes >> msess_t.txt
	done
done
