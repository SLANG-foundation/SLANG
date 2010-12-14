#! /usr/bin/python

import logging
import logging.handlers
from manager import Manager
import manager
from config import Config

# read config
c = Config("settings.xml")

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

logger.debug("Starting up...")

m = Manager(c)
m.run()
