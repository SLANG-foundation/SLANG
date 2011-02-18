#! /usr/bin/python
#
# Test client to pull some data from a SLA-NG host.
#

import time
import xmlrpclib

s = xmlrpclib.ServerProxy('http://lab-slang-1.tele2.net:8000/RPC2')

while True:
    t_start = time.time()

    # fetch data
    aggr_list = s.get_aggregate(1, 300, int(time.time()-300), int(time.time()))
    t_total = time.time() - t_start

    for aggr in aggr_list:
        print "%d: total: %3d lost: %3d success: %3d rtt: %.3f / %.3f / %.3f / %.3f / %.3f delayvar: %.3f / %.3f / %.3f / %.3f / %.3f in %f s" % \
            (
                aggr['start'], 
                aggr['total'],
                aggr['lost'], 
                aggr['success'],
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
