#! /usr/bin/python
#
# Test client to pull some data from a SLA-NG host.
#

import time
import xmlrpclib
import socket

n = 24 # How many lowres-intervals back in time to check

check = {
        'lab-slang-1.tele2.net': [1, 2, 3, 4, 5, 6, 7, 8, 9, 1155],
        'lab-slang-4.tele2.net': [1, 2, 3, 4, 5, 6, 7, 1146, 1148, 1154],
        'lab-slang-3.tele2.net': [1, 2, 3, 4, 5, 1145, 1158, 1160, 1165, 1166],
        'lab-slang-5.tele2.net': [1149, 1150, 1156, 1157, 1161]
        }

for k in check.keys():
    print "\nChecking node %s" % k
    try:
        s = xmlrpclib.ServerProxy('http://%s:8000' % k)
    except socket.error, e:
        print "Unable to connect: %s" % str(e)
        continue
    
    for sess_id in check[k]:
        res = s.get_last_dyn_aggregate(sess_id, n)
#        print "%4d: Got %d lines" % (sess_id, len(res))

        n_total = 0
        n_pingloss = 0
        n_pongloss = 0
        n_tserr = 0
        max_delayvar = 0

        for row in res:
            if int(row['aggr_interval']) != 300:
                continue
            n_total += int(row['total'])
            n_pingloss += int(row['timeout'])
            n_pongloss += int(row['pongloss'])
            n_tserr += int(row['timestamperror'])
            if int(row['delayvar_max']) > max_delayvar:
                max_delayvar = int(row['delayvar_max'])

        if n_pingloss != 0 or n_pongloss != 0:
            status = 'FAIL'
        else:
            status = 'SUCCESS'

        print ("%4d %3d: pingloss: %6d pongloss: %6d tserr: %6s delayvar_max: %6d total: %8d %8s" % 
            (sess_id, len(res), n_pingloss, n_pongloss, n_tserr, int(max_delayvar/1000), n_total, status))
