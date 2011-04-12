#!/usr/bin/python

import os
import logging
import logging.handlers
import signal
import sys
from optparse import OptionParser
#import yappi

import slang.manager
import slang.config

def daemonize (stdin='/dev/null', stdout='/dev/null', stderr='/dev/null'):
    try: 
        pid = os.fork() 
        if pid > 0:
            sys.exit(0)
    except OSError, e: 
        sys.stderr.write ("fork #1 failed: (%d) %s\n" % (e.errno, e.strerror) )
        sys.exit(1)
    os.chdir("/") 
    os.umask(0) 
    os.setsid() 
    try: 
        pid = os.fork() 
        if pid > 0:
            sys.exit(0)
    except OSError, e: 
        sys.stderr.write ("fork #2 failed: (%d) %s\n" % (e.errno, e.strerror) )
        sys.exit(1)
    si = open(stdin, 'r')
    so = open(stdout, 'a+')
    se = open(stderr, 'a+', 0)
    os.dup2(si.fileno(), sys.stdin.fileno())
    os.dup2(so.fileno(), sys.stdout.fileno())
    os.dup2(se.fileno(), sys.stderr.fileno())
     
# Read parameters
p = OptionParser()
p.add_option('-f', dest='cfg_path', default='/etc/sla-ng/manager.conf',
    help="read config from file CFG_PATH", metavar="CFG_PATH")
p.add_option('-m', dest='mode', default='bg',
    help="set daemon mode to MODE where 'bg' (default) makes the program detach run "
        "in background and anything else makes it run in foreground", 
    metavar="MODE")
p.add_option('-v', dest='verbose', action="store_true", default=False,
    help="verbose; enable debug output")

(options, args) = p.parse_args()


# set up logging
logger = logging.getLogger()
if options.verbose == True:
    logger.setLevel(logging.DEBUG)
else:
    logger.setLevel(logging.INFO)

lformat = logging.Formatter('sla-ng-manager: %(name)s: %(message)s')

# daemonize
if options.mode == 'bg':
    daemonize()
    l = logging.handlers.SysLogHandler(address='/dev/log')
    l.setFormatter(lformat)
else:
    l = logging.StreamHandler()
    l.setFormatter(lformat)

if options.verbose == True:
    l.setLevel(logging.DEBUG)
else:
    l.setLevel(logging.INFO)

logger.addHandler(l)

# start up
logger.info("Starting up SLA-NG manager...")
#yappi.start(builtins=True)

try:
    m = slang.manager.Manager(options.cfg_path)
except slang.manager.ManagerError:
    print "Cannot start manager. Exiting."
    sys.exit(0)

# set signal handlers
signal.signal(signal.SIGINT, m.sighandler)
signal.signal(signal.SIGTERM, m.sighandler)

m.run()
m.stop()
logger.info("Exiting SLA-NG manager; run() finished")

#stats = yappi.get_stats(sorttype=yappi.SORTTYPE_TSUB)
#for stat in stats:
#    print stat
#yappi.stop()

sys.exit(0)
