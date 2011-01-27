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

while True:
    try:
        d = fifo.read(128)
        # it can hold 2500 probes in fifo buff before pause
        d = unpack('llc16siillllllll16s', d)
        p.insert(d)
        # probes[id][seq][value] = timestamp
        # p2[d[0]][d[1]][d[2]] = d[3]
    except:
        print('lost ipc')
        raise
        time.sleep(1) 

p.close()
fifo.close()

