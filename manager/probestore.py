#
# Message session handling
#

import threading
import logging
import sqlite3
import time
from Queue import Queue

import config
from probe import Probe
from timespec import Timespec

class ProbeStore:
    """ Probe storage """

    lock_db = None
    lock_buf = None
    logger = None
    config = None
    db = None
#    db_conn = None
#    db_curs = None
    buf = []

    def __init__(self):
        """Constructor """

        self.logger = logging.getLogger(self.__class__.__name__)
        self.config = config.Config()
        self.lock_db = threading.Lock()
        self.lock_buf = threading.Lock()

        self.logger.debug("Created instance")

        # open database connection
        try:
            self.db = ProbeStoreDB(self.config.get_param("/config/dbpath"))
            self.db.execute("CREATE TABLE IF NOT EXISTS probes (" + 
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
                "state TEXT" + 
                ");")
            self.db.execute("CREATE INDEX IF NOT EXISTS idx_session_id ON probes(session_id)")
#            self.db_conn.commit()
            
        except Exception, e:
            self.logger.critical("Unable to open database: %s" % e)
            raise ProbeStoreError("Unable to open database: %s" % e)

#    def stop(self):

    def __del__(self):
        """ Make sure database thread is closed """
        self.logger.debug("Deleting probestore...")
        self.db.close()
        self.logger.debug("Waiting for db to die...")
        self.db.join()
        self.logger.debug("db dead.")

    def insert(self, probe):
        """ Insert probe """

        self.lock_buf.acquire()

        # insert stuff
        self.logger.debug("Received probe; id: %d seq: %d rtt: %s" % (probe.msess_id, probe.seq, probe.rtt()))
        self.buf.append(probe)

        self.lock_buf.release()

    def flush(self):
        """ Flush received probes to database """    

        self.logger.debug("Flushing probes to database")

        # create copy of buffer to reduce time it is locked
        self.lock_buf.acquire()
        tmpbuf = self.buf[:]
        self.buf = list()
        self.lock_buf.release()

        # write copied probes to database
#        self.lock_db.acquire()
        
        sql = str("INSERT INTO probes " +
            "(session_id, seq, state, t1_sec, t1_nsec, " +
            "t2_sec, t2_nsec, t3_sec, t3_nsec, " +
            "t4_sec, t4_nsec) VALUES " +
            "(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)")
        for p in tmpbuf:
            try:
                self.db.execute(sql, 
                    (p.msess_id, p.seq, p.state,
                    p.t1.sec, p.t1.nsec, p.t2.sec, p.t2.nsec,
                    p.t2.sec, p.t3.nsec, p.t4.sec, p.t4.nsec),
                )
            except Exception, e:
                self.logger.error("Unable to flush probe to database: %s" % e)
#        try:
#            self.db_conn.commit()
#        except:
#            self.logger.error("Unable to commit flushed probes to database: %s" % e)
#        self.lock_db.release()

    def delete(self, age):
        """ Deletes saved data of age 'age' and older. """
    
        self.logger.info("Deleting old data from database.")

        now = int(time.time())

#        self.lock_db.acquire()
        sql = "DELETE FROM probes WHERE t1_sec < ?"
        try:
            self.db.execute(sql, now - age)
#            self.db_conn.commit()
        except Exception, e:
            self.logger.error("Unable to delete old data: %s" % e)
#        self.lock_db.release()

class ProbeStoreError(Exception):
    pass

class ProbeStoreDB(threading.Thread):
    """ Thread-safe wrapper to sqlite interface """

    def __init__(self, db):
        threading.Thread.__init__(self)
        self.db = db
        self.reqs = Queue()
        self.logger = logging.getLogger(self.__class__.__name__)
        self.start()

    def run(self):

        conn = sqlite3.connect(self.db) 
        conn.row_factory = sqlite3.Row
        curs = conn.cursor()

        # Possible fix to get rid of commit for each line:
        # Use self.reqs.get() with a timeout, and when a timeout 
        # occurs, perform commit.
        while True:
            req, arg, res = self.reqs.get()
            if req=='--close--': 
                self.logger.info("Stopping thread...")
                break

            try:
                curs.execute(req, arg)
                conn.commit()
            except Exception, e:
                self.logger.error("Unable to execute SQL command: %s" % e)

            if res:
                for rec in curs:
                    res.put(rec)
                res.put('--no more--')

        conn.commit()
        conn.close()

    def execute(self, req, arg=None, res=None):
        """ "Execute" by pushing query to queue """

        self.reqs.put((req, arg or tuple(), res))

    def select(self, req, arg=None):
        res=Queue()
        self.execute(req, arg, res)
        while True:
            rec=res.get()
            if rec=='--no more--': break
            yield rec

    def close(self):
        self.logger.debug("Got close request, passing to thread.")
        self.execute('--close--')
