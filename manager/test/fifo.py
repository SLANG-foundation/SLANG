#!/usr/bin/python
import time
from struct import unpack 
from collections import defaultdict
from probes import probes 
# probes[id][seq][value] = timestamp
p2 = defaultdict(lambda: defaultdict(dict))
i = 0

fifo = open('/tmp/probed.fifo', 'r')
p = probes()

i=0
t = time.time()
while True:
    try:
        d = fifo.read(128)
        if len(d) < 1:
            raise Exception("len 0")
        # it can hold 2500 probes in fifo buff before pause
        # probes[id][seq][value] = timestamp
        # p2[d[0]][d[1]][d[2]] = d[3]
    except Exception, e:
        print('lost ipc: %s' % e)
        raise
        time.sleep(1) 

    i += 1
    n = 10000
    if i % n == 0:
    	interval = time.time() - t
        print "%f: %f s %f pps" % (time.time(), interval, n/interval)
	t = time.time()

p.close()
fifo.close()

