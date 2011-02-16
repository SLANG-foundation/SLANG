#! /usr/bin/python

import logging
import logging.handlers
import signal
import sys
from optparse import OptionParser

import slang.manager
import slang.config

# Read parameters
p = OptionParser()
p.add_option('-f', dest="cfg_path", default="/etc/slang.conf", help="config file path")
(options, args) = p.parse_args()

# set up logging
logger = logging.getLogger()
logger.setLevel(logging.DEBUG)

lformat = logging.Formatter('%(asctime)s %(levelname)s %(name)s: %(message)s')

ls = logging.handlers.SysLogHandler(address='/dev/log')
ls.setFormatter(lformat)
ls.setLevel(logging.DEBUG)
logger.addHandler(ls)

lc = logging.StreamHandler()
lc.setLevel(logging.DEBUG)
lc.setFormatter(lformat)
logger.addHandler(lc)

# start up
logger.debug("Starting up...")

m = slang.manager.Manager(options.cfg_path, 'slang.tele2.net')

# set signal handlers
signal.signal(signal.SIGINT, m.sighandler)
#signal.signal(signal.SIGALRM, m.sighandler)
#signal.signal(signal.SIGKILL, m.sighandler)

m.run()
logger.info("run() finished - exiting")

sys.exit(0)
