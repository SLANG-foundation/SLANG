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
                "created INTEGER, " +
                "t1 INTEGER, " + 
                "t2 INTEGER, " + 
                "t3 INTEGER, " + 
                "t4 INTEGER, " + 
                "rtt INTEGER, " +
                "delayvar INTEGER, " +
                "duplicates INTEGER, " +
                "in_order INTEGER, " +
                "state TEXT " + 
                ");")
            self.db.execute("CREATE INDEX IF NOT EXISTS idx_session_id ON probes(session_id)")
#            self.db.execute("CREATE INDEX IF NOT EXISTS idx_state ON probes(state)")
            self.db.execute("CREATE INDEX IF NOT EXISTS idx__session_id__created__state ON probes(session_id, created, state)")
            self.db.execute("CREATE INDEX IF NOT EXISTS idx__session_id__created__rtt ON probes(session_id, created, rtt)")
            self.db.execute("CREATE INDEX IF NOT EXISTS idx__rtt ON probes(rtt)")
            self.db.execute("CREATE INDEX IF NOT EXISTS idx__created ON probes(created)")
            
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
            "created, t1, t2, t3, t4, " +
            "rtt, delayvar, "+
            "in_order, duplicates)" +
            " VALUES " +
            "(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)")

        dv = None
        if p.delay_variation is not None:
            dv = abs(p.delay_variation)

        try:
            self.db.execute(sql, 
                    (p.session_id, p.seq, p.state, p.created,
                     p.t1, p.t2, p.t2, p.t4, 
                     p.rtt, dv,
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

        sql = "DELETE FROM probes WHERE created < ?"
        try:
            self.db.execute(sql, ((now - age)*1000000000, ))
        except Exception, e:
            self.logger.error("Unable to delete old data: %s" % e)


    ###################################################################
    #
    # Functions to fetch data from database
    #

    def current_sessions(self):
        """ Get IDs of current sessions """
        
        sql = "SELECT DISTINCT(session_id) AS session_id FROM probes"
        res = self.db.select(sql)
        retval = list()
        for r in res:
            retval.append(r['session_id'])

        return retval


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

        sql = "SELECT * FROM probes WHERE session_id = ? AND created > ? AND created < ?"
        res = self.db.select(sql, (session_id, start, end))

        pset = ProbeSet()
        for row in res:
            pdlist = (
                row['created'],
                row['state'], '', row['session_id'], row['seq'],
                row['t1'], row['t2'], row['t3'], row['t4'], None, row['rtt'], row['delayvar']
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
            'delayvar_max': 0,
            'delayvar_min': 0,
            'delayvar_avg': 0,        
            'delayvar_med': 0,        
            'delayvar_95th': 0,        
        }

        where = "session_id = ? AND created >= ? AND created < ?"
        whereargs = (session_id, start, end)
        
        # get number of probes - needed for percentile and percentage calculations
        nrows = 0
        sql = "SELECT COUNT(*) AS count FROM probes WHERE " + where
        res = self.db.select(sql, whereargs)
        for row in res:
            if row['count'] == 0:
                self.logger.debug("No data for session %d from %d to %d" % (session_id, start, end))
                return retdata
            else:
                retdata['total'] = row['count']

#        self.logger.debug("Aggregating session %d from %d to %d, %d rows" % (session_id, start, end, retdata['total'], ))

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
        sql = ("SELECT MAX(rtt) AS max, MIN(rtt) AS min, " + 
            "AVG(rtt) AS avg FROM probes " + 
            "WHERE rtt IS NOT NULL AND " + where)
        res = self.db.select(sql, whereargs)
        for row in res:
#            self.logger.debug("got max: %s min: %s avg: %s" % (row['max'], row['min'], row['avg']))
            retdata['rtt_max'] = row['max']
            retdata['rtt_min'] = row['min']
            retdata['rtt_avg'] = row['avg']

        sql = ("SELECT MAX(delayvar) AS max, MIN(delayvar) AS min, " + 
            "AVG(delayvar) AS avg FROM probes " + 
            "WHERE delayvar IS NOT NULL AND " + where)
        res = self.db.select(sql, whereargs)
        for row in res:
#            self.logger.debug("got max: %s min: %s avg: %s" % (row['max'], row['min'], row['avg']))
            retdata['delayvar_max'] = row['max']
            retdata['delayvar_min'] = row['min']
            retdata['delayvar_avg'] = row['avg']

        # get percentiles
        retdata['rtt_med'] = self.get_percentile(session_id, start, end, "rtt", 50)
        retdata['rtt_95th'] = self.get_percentile(session_id, start, end, "rtt", 95)
        retdata['delayvar_med'] = self.get_percentile(session_id, start, end, "delayvar", 50)
        retdata['delayvar_95th'] = self.get_percentile(session_id, start, end, "delayvar", 95)

        return retdata

    def get_percentile(self, session_id, start, end, type, percentile):
        """ Get percentileth percentile of RTT or delay variation.  
        
            TODO:
            Look for different states for rtt/delayvar?
        """

        if type == "rtt":
            column = "rtt"
        elif type == "delayvar":
            column = "delayvar"
        else:
            return None

        where = "session_id = ? AND created >= ? AND created < ? AND state = ?"
        whereargs = (session_id, start, end, probe.STATE_READY)

        sql = "SELECT COUNT(*) AS count FROM probes WHERE " + where
        res = self.db.select(sql, whereargs)
        for row in res:
            nrows = row['count']

        if nrows < 1:
            return None

        sql = ("SELECT " + column + " AS percentile FROM probes WHERE " + where +
            " ORDER BY " + column + " ASC LIMIT 1 OFFSET CAST(? * ? / 100 AS INTEGER)")

        res = self.db.select(sql, whereargs + (nrows, percentile))

        for row in res:
            return row['percentile']

        # We really never should get here...
        return None


    def get_storage_statistics(self):
        """ Get database statistics """

        retval = {}

        # Get number of rows in database
        sql = "SELECT COUNT(*) AS c FROM probes"
        res = self.db.select(sql)
        for row in res:
            retval['db_numrows'] = row['c']
        
        # number of sessions in state dict
        retval['state_numsessions'] = len(self.probes)

        # number of probes in state dict
        retval['state_numprobes'] = 0
        for session in self.probes:
            retval['state_numprobes'] += len(self.probes[session])

        return retval


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

            if req.lower().find("delete from ") >= 1:
                self.logger.debug("Deleted %d rows." % curs.rowcount)

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
