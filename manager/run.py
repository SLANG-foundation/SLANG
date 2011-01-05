#! /usr/bin/python

import logging
import logging.handlers
from manager import Manager
import manager
from config import Config

# set up logging
logger = logging.getLogger()
logger.setLevel(logging.DEBUG)

lformat = logging.Formatter('%(asctime)s %(levelname)s %(name)s: %(message)s')

lc = logging.StreamHandler()
lc.setLevel(logging.DEBUG)
lc.setFormatter(lformat)
logger.addHandler(lc)

ls = logging.handlers.SysLogHandler()
ls.setFormatter(lformat)
logger.addHandler(ls)

# read config
c = Config("../probed/settings.xml")

# start up
logger.debug("Starting up...")

m = Manager()
m.run()
