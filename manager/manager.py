#!/usr/bin/python

import os
import logging
import logging.handlers
import signal
import sys
from optparse import OptionParser

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
p.add_option('-f', dest="cfg_path", default="/etc/sla-ng/manager.conf", 
    help="config file path")
(options, args) = p.parse_args()

# daemonize
daemonize()

# set up logging
logger = logging.getLogger()
logger.setLevel(logging.INFO)

lformat = logging.Formatter('manager: %(name)s: %(message)s')

ls = logging.handlers.SysLogHandler(address='/dev/log')
ls.setFormatter(lformat)
ls.setLevel(logging.DEBUG)
logger.addHandler(ls)

# start up
logger.debug("Starting up SLA-NG manager...")

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

sys.exit(0)
