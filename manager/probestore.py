#
# Message session handling
#

import threading
import logging
import sqlite3
import time
from Queue import Queue, Empty

import config
from probe import Probe, ProbeSet
import probe
from timespec import Timespec

class ProbeStore:
    """ Probe storage """

    lock_db = None
    lock_buf = None
    logger = None
    config = None
    db = None
    probes = None
    max_seq = None

    def __init__(self):
        """Constructor """

        self.logger = logging.getLogger(self.__class__.__name__)
        self.config = config.Config()
        self.lock_buf = threading.Lock()

        self.probes = dict()
        self.max_seq = dict()

        self.logger.debug("Created instance")

        # open database connection
        try:
            self.db = ProbeStoreDB(self.config.get_param("/config/dbpath"))
            self.db.execute("CREATE TABLE IF NOT EXISTS probes (" + 
                "session_id INTEGER, " +
                "seq INTEGER, " + 
                "created_sec INTEGER, " +
                "created_nsec INTEGER, " +
                "t1_sec INTEGER, " + 
                "t1_nsec INTEGER, " + 
                "t2_sec INTEGER, " + 
                "t2_nsec INTEGER, " + 
                "t3_sec INTEGER, " + 
                "t3_nsec INTEGER, " + 
                "t4_sec INTEGER, " + 
                "t4_nsec INTEGER, " + 
                "rtt_nsec INTEGER, " +
                "delayvar_nsec INTEGER, " +
                "duplicates INTEGER, " +
                "in_order INTEGER, " +
                "state TEXT " + 
                ");")
            self.db.execute("CREATE INDEX IF NOT EXISTS idx_session_id ON probes(session_id)")
#            self.db.execute("CREATE INDEX IF NOT EXISTS idx_state ON probes(state)")
            self.db.execute("CREATE INDEX IF NOT EXISTS idx_session_id__created_sec__state ON probes(session_id, created_sec, state)")
            self.db.execute("CREATE INDEX IF NOT EXISTS idx_session_id__created_sec__rtt_nsec ON probes(session_id, created_sec, rtt_nsec)")
            self.db.execute("CREATE INDEX IF NOT EXISTS idx_rtt_nsec ON probes(rtt_nsec)")
            self.db.execute("CREATE INDEX IF NOT EXISTS idx_created_sec ON probes(created_sec)")
            
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

    def add(self, p):
        """ Add probe to ProbeStore 

            TODO:
            Handle duplicate packets!
        
            Logic when new probe arrives:
            V2:
            if p.seq > max_seq:
                in order
            else:
                not in order

            if we have seq - 1:
                Calculate delay var for current packet
                set has_given on seq - 1

            if we have seq + 1:
                Calculate delay var for seq + 1
                set has_given on seq

            for i in range(seq-1 seq+2):
                try:
                    if self.probes[p.session_id][i].has_given and self.probes[p.session_id][i].has_delayvar
                        self.insert(self.probes[p.session_id][i])
                        del self.probes[p.session_id][i]
                except KeyError:
                    continue
                        

            save current to list
            set last seq to current

        """

        if not p.session_id in self.max_seq:
            # No max sequence number for current measurement session.
            # Probably new session. Add packet and mark as has gotten delay variation.
            self.max_seq[p.session_id] = p.seq
            p.has_gotten = True
            if p.session_id not in self.probes:
                self.probes[p.session_id] = dict()
            self.probes[p.session_id][p.seq] = p
            return

        # Reordered?
        if  p.seq > self.max_seq[p.session_id]:
            p.in_order = True
            self.max_seq[p.session_id] = p.seq
        else:
            p.in_order = False
        
        # check for previous packet
        try:
            p.set_prev_probe(self.probes[p.session_id][p.seq - 1])
        except KeyError:
            pass
        
        # check for next packet
        try:
            self.probes[p.session_id][p.seq + 1].set_prev_probe(p)
        except KeyError:
            pass

        # save probe and update last_seq
        if p.session_id not in self.probes: # Can this occur, given that we check it when we add new element to self.last_seq?
            self.probes[p.session_id] = dict()
        self.probes[p.session_id][p.seq] = p
            
        # see if packets are ready to save
        for i in range(p.seq-1, p.seq+2):
            try:
                if self.probes[p.session_id][i].has_given and self.probes[p.session_id][i].has_gotten:
                    self.insert(self.probes[p.session_id][i])
                    del self.probes[p.session_id][i]
            except KeyError:
                continue


    def insert(self, p):
        """ Insert probe into database """

        sql = ("INSERT INTO probes " +
            "(session_id, seq, state, " + 
            "created_sec, created_nsec, " +
            "t1_sec, t1_nsec, " +
            "t2_sec, t2_nsec, " + 
            "t3_sec, t3_nsec, " +
            "t4_sec, t4_nsec, " +
            "rtt_nsec, delayvar_nsec, "+
            "in_order, duplicates)" +
            " VALUES " +
            "(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)")

        try:
            self.db.execute(sql, 
                    (p.session_id, p.seq, p.state, p.created.sec, p.created.nsec,
                     p.t1.sec, p.t1.nsec, p.t2.sec, p.t2.nsec,
                     p.t2.sec, p.t3.nsec, p.t4.sec, p.t4.nsec, 
                     p.rtt.to_nanoseconds(), p.delay_variation.to_nanoseconds(),
                     p.in_order, 0
                    ),
            )
                      
        
        except Exception, e:
            self.logger.error("Unable to flush probe to database: %s: " % e)

    def flush(self):
        """ Requesting commit """    

        self.logger.debug("Requesting commit")

        self.db.commit()

    def delete(self, age):
        """ Deletes saved data of age 'age' and older. """
    
        self.logger.info("Deleting old data from database.")

        now = int(time.time())

        sql = "DELETE FROM probes WHERE created_sec < ?"
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

        #self.logger.debug("Getting raw data for id %d start %d end %d" % (session_id, start, end))

        sql = "SELECT * FROM probes WHERE session_id = ? AND created_sec > ? AND created_sec < ?"
        res = self.db.select(sql, (session_id, start, end))

        pset = ProbeSet()
        for row in res:
            pdlist = (
                row['created_sec'], row['created_nsec'],
                row['state'], '', row['session_id'], row['seq'],
                row['t1_sec'], row['t1_nsec'],
                row['t2_sec'], row['t2_nsec'],
                row['t3_sec'], row['t3_nsec'],
                row['t4_sec'], row['t4_nsec'],
                )
            pset.append(Probe(pdlist))

        return pset

    def get_aggregate(self, session_id, start, end):
        """ Get round-trip time

            Get average round-trip time for measurement session session_id
            whose probes were created as end < created <= end.
        """

        retdata = {
            'total': 0,
            'lost': 0,
            'success': 0,
            'rtt_max': 0,
            'rtt_min': 0,
            'rtt_avg': 0,        
            'rtt_med': 0,        
            'rtt_95th': 0,        
        }

        where = "session_id = ? AND created_sec >= ? AND created_sec < ?"
        whereargs = (session_id, start.sec, end.sec)
        
        # get number of probes - needed for percentile and percentage calculations
        nrows = 0
        sql = "SELECT COUNT(*) AS count FROM probes WHERE " + where
        res = self.db.select(sql, whereargs)
        for row in res:
            if row['count'] == 0:
                self.logger.debug("No data for session %d from %d to %d" % (session_id, start.sec, end.sec))
                return retdata
            else:
                retdata['total'] = row['count']

#        self.logger.debug("Aggregating session %d from %d to %d, %d rows" % (session_id, start.sec, end.sec, retdata['total'], ))

        # get percentages
        sql = "SELECT CAST( COUNT(*) AS REAL) AS lost FROM PROBES WHERE state = ? AND " + where
        res = self.db.select(sql, (probe.STATE_TIMEOUT, ) + whereargs)
        for row in res:
            retdata['loss'] = row['lost']

        sql = "SELECT CAST( COUNT(*) AS REAL) AS success FROM PROBES WHERE state = ? AND " + where
        res = self.db.select(sql, (probe.STATE_READY, ) + whereargs)
        for row in res:
            retdata['success'] = row['success']


        # get max, min and average
        sql = ("SELECT MAX(rtt_nsec) AS max, MIN(rtt_nsec) AS min, " + 
            "AVG(rtt_nsec) AS avg FROM probes " + 
            "WHERE rtt_nsec IS NOT NULL AND " + where)
        res = self.db.select(sql, whereargs)
        for row in res:
#            self.logger.debug("got max: %s min: %s avg: %s" % (row['max'], row['min'], row['avg']))
            retdata['rtt_max'] = row['max']
            retdata['rtt_min'] = row['min']
            retdata['rtt_avg'] = row['avg']

        # get percentiles
        retdata['rtt_med'] = self.get_rtt_percentile(session_id, start, end, 50)
        retdata['rtt_95th'] = self.get_rtt_percentile(session_id, start, end, 95)

        return retdata

    def get_rtt_percentile(self, session_id, start, end, percentile):
        """ Get percentileth percentile of RTT. """

        where = "session_id = ? AND created_sec >= ? AND created_sec < ? AND state = ?"
        whereargs = (session_id, start.sec, end.sec, probe.STATE_READY)

        sql = "SELECT COUNT(*) AS count FROM probes WHERE " + where
        res = self.db.select(sql, whereargs)
        for row in res:
            nrows = row['count']

        if nrows < 1:
            return None

        sql = ("SELECT rtt_nsec AS percentile FROM probes WHERE " + where +
            " ORDER BY rtt_nsec ASC LIMIT 1 OFFSET CAST(? * ? / 100 AS INTEGER)")

        res = self.db.select(sql, whereargs + (nrows, percentile))

        for row in res:
            return row['percentile']

        # We really never should get here...
        return None


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

        while True:
          try:
              conn = sqlite3.connect(self.db) 
              conn.row_factory = sqlite3.Row
              curs = conn.cursor()
              break
          except Exception, e:
              self.logger.error("Unable to establish database connection: %s. Retrying." % e)
              time.sleep(1)

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

            # fetch query from queue.
            req, arg, res = self.reqs.get()

            # Close command?
            if req == '--close--': 
                self.logger.info("Stopping thread...")
                break

            # commit?
            if req == '--commit--':
                conn.commit()
                self.logger.info("Committing %d queries." % exec_c)
                exec_c = 0
                continue

            try:
                curs.execute(req, arg)
                exec_c += 1
            except Exception, e:
                self.logger.error("Unable to execute SQL command (%s): %s" % (req, e))

            # handle response from select queries
            if res:
                for rec in curs:
                    res.put(rec)
                res.put('--no more--')

            # If we have 10000 outstanding executions, perform a commit.
            if exec_c >= 10000:
                self.logger.info("Reached 10000 outstanding queries. Committing transaction.")
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

    def commit(self):
        self.execute('--commit--')
