#
# Message session handling
#

import logging
import sqlite3

import config

class Msess:

  logger = None
  config = None
  db_conn = None
  db_curs = None

  def __init__(self):
    """Constructor
    """

    self.logger = logging.getLogger(self.__class__.__name__)
    self.config = config.Config()

    self.logger.debug("Created instance")

    # open database connection
    try:
      self.db_conn = sqlite3.connect(self.config.getParam("/config/dbpath"))
      self.db_conn.row_factory = sqlite3.Row
      self.db_curs = self.db_conn.cursor()
      self.db_curs.execute("CREATE TABLE IF NOT EXISTS probes (" + 
        "session_id INTEGER," +
        "seq INTEGER," + 
        "t1_sec INTEGER," + 
        "t1_nsec INTEGER," + 
        "t2_sec INTEGER," + 
        "t2_nsec INTEGER," + 
        "t3_sec INTEGER," + 
        "t3_nsec INTEGER," + 
        "t4_sec INTEGER," + 
        "t4_nsec INTEGER," + 
        "state INTEGER" + 
        ");")
      self.db_curs.execute("CREATE INDEX IF NOT EXISTS idx_session_id ON probes(session_id)")
      
    except:
      self.logger.critical("Unable to open database")
      raise MsessError("Unable to open database")
      
class MsessError(Exception):
  pass
