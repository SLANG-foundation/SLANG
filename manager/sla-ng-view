#!/usr/bin/python

import sys
from optparse import OptionParser

# Read parameters
o = OptionParser()
o.add_option('-f', dest='cfg_path', default='/etc/sla-ng/manager.conf')
o.add_option('-i', dest='sessid', default=-1)
o.add_option('-m', dest='mode', default='ping')
o.add_option('-t', dest='interval', default='300')
(options, args) = o.parse_args()

if options.mode == 'ping':
    import slang.probe
    import slang.config
    # Open FIFO
    config = slang.config.Config(options.cfg_path)
    print 'Starting SLA-NG ping viewer, connecting to FIFO...'
    sys.stdout.flush()
    fifo = open(config.get('fifopath'), 'r');

    while True:
        data = fifo.read(24)
        if len(data) < 1:
            continue
        p = slang.probe.from_struct(data)
        if int(options.sessid) >= 0:
            if int(p.session_id) != int(options.sessid):
                continue
        print p
        sys.stdout.flush()

if options.mode == 'aggr':
    import time
    import xmlrpclib
    print 'Starting SLA-NG aggr data viewer, connecting to RPC...'
    sys.stdout.flush()
    s = xmlrpclib.ServerProxy('http://127.0.0.1:8000/RPC2')
    while True:
        # fetch data
        print 'Calculating aggregated values...'
        sys.stdout.flush()
        a = s.get_aggregate(int(options.sessid), int(options.interval),
            int(time.time()-int(options.interval)), int(time.time()))

        for aggr in a:
            print "OK: %.0f, LOSS: Ping: %.0f, Pong: %.0f, DSCP: %.0f, TS: %.0f, DUP: %.0f, OOO: %.0f\nRTT: %.3f / %.3f / %.3f / %.3f / %.3f\nJitter: %.3f / %.3f / %.3f / %.3f / %.3f" % \
                (
                    float(aggr['success']),
                    float(aggr['timeout']),
                    float(aggr['pongloss']),
                    float(aggr['dscperror']),
                    float(aggr['timestamperror']),
                    float(aggr['dup']),
                    float(aggr['reordered']),
                    float(aggr['rtt_max']) / float(1000),
                    float(aggr['rtt_avg']) / float(1000),
                    float(aggr['rtt_med']) / float(1000),
                    float(aggr['rtt_min']) / float(1000),
                    float(aggr['rtt_95th']) / float(1000),
                    float(aggr['delayvar_max']) / float(1000),
                    float(aggr['delayvar_avg']) / float(1000),
                    float(aggr['delayvar_med']) / float(1000),
                    float(aggr['delayvar_min']) / float(1000),
                    float(aggr['delayvar_95th']) / float(1000)
                )
        sys.stdout.flush()
        time.sleep(int(options.interval))
