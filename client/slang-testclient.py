#! /usr/bin/python
#
# Test client to pull some data from a SLA-NG host.
#

import time
import xmlrpclib

s = xmlrpclib.ServerProxy('http://127.0.0.1:8000/RPC2')

while True:
    t_start = time.time()

    # fetch data
    aggr_list = s.get_aggregate(1, 1, int(time.time()-20), int(time.time()-19))
    t_total = time.time() - t_start

    for aggr in aggr_list:
        print "%d: tot: %3d success: %3d tserr: %3d pongloss: %3d timeout: %3d dup: %3d rtt: %.3f / %.3f / %.3f / %.3f / %.3f delayvar: %.3f / %.3f / %.3f / %.3f / %.3f in %f s" % \
            (
                aggr['start'], 
            aggr['total'],
            aggr['success'],
            aggr['timestamperror'],
            aggr['pongloss'],
            aggr['timeout'],
            aggr['dup'],
                float(aggr['rtt_max']) / float(1000), 
                float(aggr['rtt_avg']) / float(1000), 
                float(aggr['rtt_med']) / float(1000), 
                float(aggr['rtt_min']) / float(1000), 
                float(aggr['rtt_95th']) / float(1000), 
                float(aggr['delayvar_max']) / float(1000), 
                float(aggr['delayvar_avg']) / float(1000), 
                float(aggr['delayvar_med']) / float(1000), 
                float(aggr['delayvar_min']) / float(1000), 
                float(aggr['delayvar_95th']) / float(1000),
                t_total
            )

    time.sleep(1)
