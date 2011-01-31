#
# Message session handling
#

import threading
import logging
import sqlite3
import time
from Queue import Queue, Empty

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

    def stop(self):
        """ Close down ProbeStore """
        self.logger.debug("Closing probestore...")
        self.db.close()
        self.logger.debug("Waiting for db to die...")
        self.db.join()
        self.logger.debug("db dead.")

    def insert(self, probe):
        """ Insert probe """

        self.lock_buf.acquire()

        # insert stuff
#        self.logger.debug("Received probe; id: %d seq: %d rtt: %s" % (probe.msess_id, probe.seq, probe.rtt()))
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

        self.logger.debug("Flush complete")

    def delete(self, age):
        """ Deletes saved data of age 'age' and older. """
    
        self.logger.info("Deleting old data from database.")

        now = int(time.time())

        sql = "DELETE FROM probes WHERE t1_sec < ?"
        try:
            self.db.execute(sql, (now - age, ))
        except Exception, e:
            self.logger.error("Unable to delete old data: %s" % e)

    ###################################################################
    #
    # Functions to fetch data from database
    #

    def get_raw(self, session_id, start, end=None):
        """ Get raw data from database.

        Returns raw measurement data from database.
        Arguments:
        session_id -- ID of the session data is requested for.
        start -- Start time.
        stop -- End time.

        Data is returned as a list of dicts.

        """

        if end is None:
            end = int(time.Time())

        self.logger.debug("Getting raw data for id %d start %d end %d" % (session_id, start, end))

        sql = "SELECT * FROM probes WHERE session_id = ? AND t1_sec > ? AND t1_sec < ?"
        res = self.db.select(sql, (session_id, start, end))

        retlist = []
        for row in res:
            retlist.append(
                { 
                    'seq': row['seq'], 
                    'state': row['state'], 
                    't1_sec': row['t1_sec'], 
                    't1_nsec': row['t1_nsec'], 
                    't2_sec': row['t2_sec'], 
                    't2_nsec': row['t2_nsec'], 
                    't3_sec': row['t3_sec'], 
                    't3_nsec': row['t3_nsec'], 
                    't4_sec': row['t4_sec'], 
                    't4_nsec': row['t4_nsec'], 
                })

        return retlist
    

class ProbeStoreError(Exception):
    pass

class ProbeStoreDB(threading.Thread):
    """ Thread-safe wrapper to sqlite interface """

    def __init__(self, db):
        threading.Thread.__init__(self)
        self.db = db
        self.reqs = Queue()
        self.logger = logging.getLogger(self.__class__.__name__)
        self.logger.debug("Created instance")
        self.start()

    def run(self):

        self.logger.debug("Starting database thread...")

        try:
            conn = sqlite3.connect(self.db) 
            conn.row_factory = sqlite3.Row
            curs = conn.cursor()
        except Exception, e:
            self.logger.error("Unable to establish database connection: %s" % e)

        # Possible fix to get rid of commit for each line:
        # Use self.reqs.get() with a timeout, and when a timeout 
        # occurs, perform commit.
        #
        # Another way would be to keep count of how many execute()s we 
        # have run and commit when we reach a certain value.
        #
        # Or, we can do both.
        exec_c = 0
        while True:

            #self.logger.debug("Fetching query from queue. Approximative queue length: %d", self.reqs.qsize())

            # fetch query from queue with a timeout.
            try:
                req, arg, res = self.reqs.get(True, 3)
            except Empty, e:
                self.logger.debug("Queue timeout occurred. Committing transaction of %d queries." % exec_c)
                conn.commit()
                exec_c = 0
                continue

            # Close command?
            if req=='--close--': 
                self.logger.info("Stopping thread...")
                break

            try:
                curs.execute(req, arg)
                exec_c += 1
            except Exception, e:
                self.logger.error("Unable to execute SQL command: %s" % e)

            # handle response from select queries
            if res:
                for rec in curs:
                    res.put(rec)
                res.put('--no more--')

            # If we have 10000 outstanding executions, perform a commit.
            if exec_c >= 10000:
                self.logger.debug("Reached 10000 outstanding queries. Committing transaction.")
                conn.commit()
                exec_c = 0

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
