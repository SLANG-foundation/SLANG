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

    logger = None
    config = None
    db = None
    probes = None
    max_seq = None
    flag_flush_queue = False

    # Low resolution aggretation interval
    AGGR_DB_LOWRES = 300

    # High resolution aggregation interval, used when errors occur
    AGGR_DB_HIGHRES = 1
    
    # How long time, in seconds, before and after interesting event to
    # store hugh resolution data.
    HIGHRES_INTERVAL = 10

    def __init__(self):
        """Constructor """

        self.logger = logging.getLogger(self.__class__.__name__)
        self.config = config.Config()

        self.probes = dict()
        self.max_seq = dict()

        self.logger.debug("created instance")

        # open database connection
        try:
            self.db = ProbeStoreDB(self.config.get('dbpath'))

            # create probe table
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
            self.db.execute("CREATE INDEX IF NOT EXISTS idx__session_id ON probes(session_id)")
            self.db.execute("CREATE INDEX IF NOT EXISTS idx__session_id__created__state ON probes(session_id, created, state)")
            self.db.execute("CREATE INDEX IF NOT EXISTS idx__session_id__created__rtt ON probes(session_id, created, rtt)")
            self.db.execute("CREATE INDEX IF NOT EXISTS idx__session_id__created__in_order ON probes(session_id, created, in_order)")
            self.db.execute("CREATE INDEX IF NOT EXISTS idx__rtt ON probes(rtt)")
            self.db.execute("CREATE INDEX IF NOT EXISTS idx__created ON probes(created)")

            # create aggregate database
            self.db.execute("CREATE TABLE IF NOT EXISTS probes_aggregate (" + 
                "session_id INTEGER, " +
                "aggr_interval INTEGER, " + 
                "created INTEGER, " +
                "total INTEGER, " +
                "success INTEGER, " +
                "timestamperror INTEGER, " +
                "dscperror INTEGER, " +
                "pongloss INTEGER, " +
                "timeout INTEGER, " +
                "dup INTEGER, " +
                "reordered INTEGER, " +
                "rtt_min INTEGER, " + 
                "rtt_med INTEGER, " + 
                "rtt_avg INTEGER, " + 
                "rtt_max INTEGER, " + 
                "rtt_95th INTEGER, " + 
                "delayvar_min INTEGER, " + 
                "delayvar_med INTEGER, " + 
                "delayvar_avg INTEGER, " + 
                "delayvar_max INTEGER, " + 
                "delayvar_95th INTEGER " + 
                ");")

            self.db.execute("CREATE INDEX IF NOT EXISTS idx__session_id ON probes_aggregate(session_id)")
            self.db.execute("CREATE INDEX IF NOT EXISTS idx__created ON probes_aggregate(created)")
            self.db.execute("CREATE INDEX IF NOT EXISTS idx__sesson_id__idx__created ON probes_aggregate(session_id, created)")
            
        except Exception, e:
            estr = "Unable to initialize database: %s" % str(e)
            self.logger.critical(estr)
            raise ProbeStoreError(estr)

    def flush_queue(self):
        """ Schedule a probe queue flush. """

        self.flag_flush_queue = True


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

        # Should queue be flushed?
        if self.flag_flush_queue:
            self.probes = dict()
            self.max_seq = dict()
            self.flag_flush_queue = False

        # duplicate packet?
        if p.state == probe.STATE_DUP:
            
            # Do we have session in list? If not, discard.
            if p.session_id not in self.probes:
                return

            # Do we have the sequence number in list?
            # If not, the result has probably been written to database already.
            if p.seq in self.probes[p.session_id]:
                self.probes[p.session_id][p.seq].dups += 1
            else:
                sql = ("UPDATE probes SET duplicates = duplicates + 1 " +
                    "WHERE session_id = ? AND seq = ?")
                self.db.execute(sql, (p.session_id, p.seq))

            return

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
        if p.state == probe.STATE_OK:
            if p.seq > self.max_seq[p.session_id]:
                p.in_order = True
                self.max_seq[p.session_id] = p.seq
            else:
                # if the difference is too big, a counter probably
                # flipped over
                if self.max_seq[p.session_id] - p.seq > 1000000:
                    p.in_order = True
                    self.max_seq[p.session_id] = p.seq
                else:
                    p.in_order = False
        else:
            p.in_order = True
        
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
                     p.in_order, p.dups
                    ),
            )
                      
        
        except Exception, e:
            self.logger.error("Unable to flush probe to database: %s: " % e)


    def flush(self):
        """ Requesting commit """    

        self.logger.debug("Requesting commit")

        self.db.commit()


    def delete(self, age, age_lowres):
        """ Deletes saved data of age 'age' and older. """
    
        self.logger.info("Deleting old data from database.")

        now = int(time.time())

        sql = "DELETE FROM probes WHERE created < ?"
        self.db.execute(sql, ((now - age)*1000000000, ))

        sql = "DELETE FROM probes_aggregate WHERE created < ? AND aggr_interval = ?"
        self.db.execute(sql, ((now - age)*1000000000, self.AGGR_DB_HIGHRES))

        sql = "DELETE FROM probes_aggregate WHERE created < ?"
        self.db.execute(sql, ((now - age_lowres)*1000000000, ))


    def aggregate(self, sess_id, start):
        """ Aggregates self.AGGR_DB_LOWRES seconds worth of data. 
        
            Starts att time 'start'. The result will be written to the
            database.
        """

        self.logger.debug("aggregating session %d from %d, %d seconds interval" % 
            (sess_id, start, self.AGGR_DB_LOWRES))

        # get aggregated data
        data = self.get_aggregate(sess_id, 
            start, 
            start + self.AGGR_DB_LOWRES * 1000000000)

        # insert into database
        sql = ("INSERT INTO probes_aggregate " +
            "(session_id, aggr_interval, created, total, " +
            "success, timestamperror, dscperror, pongloss, timeout, " +
            "dup, reordered, rtt_min, rtt_med, rtt_avg, rtt_max, " +
            "rtt_95th, delayvar_min, delayvar_med, delayvar_avg, " +
            "delayvar_max, delayvar_95th) VALUES " +
            "(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ? )")
        params = (
            sess_id, self.AGGR_DB_LOWRES, start, data['total'], data['success'],
            data['timestamperror'], data['dscperror'], data['pongloss'], data['timeout'], 
            data['dup'], data['reordered'], data['rtt_min'], data['rtt_med'], data['rtt_avg'], 
            data['rtt_max'], data['rtt_95th'], data['delayvar_min'], 
            data['delayvar_med'], data['delayvar_avg'], data['delayvar_max'], 
            data['delayvar_95th'])

        self.db.execute(sql, params)

        #
        # compare aggregated data to previous data to find interesting events
        #
        # How to check for interesting events?
        #
        # We have a few metrics:
        # rtt
        # delay variation 
        # packet loss (pongloss, timeouts)
        # DSCP errors
        # 
        # What conditions of the metrics above indicates an interesting 
        # event (not necessarily an error)?
        # * changed RTT (new route)
        #  - What to look at? max/avg/min/95th? MIN interesting!
        # 
        # * changed delay variation 
        #  - What to look for here? 
        # 
        # * packet loss
        #  - Always interesting, should not happen very often...
        # 
        # * DSCP? When instant change?
        #
        # Let's start with:
        # RTT: - SKIPPED!
        #  * abs(avg(rtt_min last X periods) - rtt_min) > 10% of avg(rtt_min last X periods) 
        #
        # Packet loss:
        #  * _any_ packet loss (pongloss, timeout) is interesting.
        #

        # Look for packet loss in aggregated data
        if data['timeout'] > 0 or data['pongloss'] > 0:

            self.logger.debug( ("Got interesting event: sess_id: %d " +
                "timeout: %d pongloss: %d") % 
                (sess_id, data['timeout'], data['pongloss']))

            # Something happened. Get higher resolution data with some
            # overlap to always be able to present a full interval around an 
            # error. Begin with calculating intervals.
            ctime = start - self.HIGHRES_INTERVAL * 1000000000
            aggr_times = list()
            while ctime < start + (self.AGGR_DB_LOWRES + self.HIGHRES_INTERVAL) * 1000000000:
                aggr_times.append(ctime)
                ctime += self.AGGR_DB_HIGHRES * 1000000000

            aggr_times.append(start + (self.AGGR_DB_LOWRES + self.HIGHRES_INTERVAL) * 1000000000)
    
            # fetch data for intervals
            highres = list()
            for i in range(0, len(aggr_times) - 1):
                r = self.get_aggregate(sess_id, aggr_times[i], aggr_times[i+1])
                r['start'] = aggr_times[i]
                highres.append(r)

            # go through higher resolution data, find where interesting event occurred.
            # Skip first part as it is outside the current interval
            idx_to_examine = list()
            for i in range(int(self.HIGHRES_INTERVAL/self.AGGR_DB_HIGHRES) , len(highres) - int(self.HIGHRES_INTERVAL/self.AGGR_DB_HIGHRES)):

                # was there an error in the interval? If so, add index to list.
                if highres[i]['timeout'] > 0 or highres[i]['pongloss'] > 0:
                    idx_to_examine.append(i)

#            self.logger.debug("will examine %d records deeper (%s)" % (len(idx_to_examine), idx_to_examine))

            # append interesting records to list of records to save
            highres_save = list()
            idx_per_second = 1/float(self.AGGR_DB_HIGHRES)
            for i in range(0, len(idx_to_examine) - 1):

                low = idx_to_examine[i] - int(idx_per_second * self.HIGHRES_INTERVAL)
                high = idx_to_examine[i] + int(idx_per_second * self.HIGHRES_INTERVAL) + 1

                # if there is a previous row, make sure we do not overlap
                # Might we have some rounding issues here?
                if i != 0:
                    prev_top_elem = idx_to_examine[i-1] + int(idx_per_second * self.HIGHRES_INTERVAL) 
                    if prev_top_elem >= low:
                        low = prev_top_elem + 1

                # make sure we do not walk outside out list
                if high > len(highres) - 1:
                    high = len(highres) - 1

                highres_save += highres[low:high]

            self.logger.debug("Will write %d extra rows to database" % len(highres_save))
                    
            # save data
            for row in highres_save:
                sql = ("INSERT INTO probes_aggregate " +
                    "(session_id, aggr_interval, created, total, " +
                    "success, timestamperror, pongloss, timeout, " +
                    "dup, reordered, rtt_min, rtt_med, rtt_avg, rtt_max, " +
                    "rtt_95th, delayvar_min, delayvar_med, delayvar_avg, " +
                    "delayvar_max, delayvar_95th) VALUES " +
                    "(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ? )")

                params = (sess_id, self.AGGR_DB_HIGHRES, row['start'], 
                    row['total'], row['success'], row['timestamperror'], 
                    row['pongloss'], row['timeout'], row['dup'], row['reordered'], 
                    row['rtt_min'], row['rtt_med'], row['rtt_avg'], row['rtt_max'], 
                    row['rtt_95th'], row['delayvar_min'], row['delayvar_med'], 
                    row['delayvar_avg'], row['delayvar_max'], 
                    row['delayvar_95th'])
        
                self.db.execute(sql, params)


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

    def get_last_lowres_aggregate(self, session_id, num = 1):
        """ Get last precomputed aggregate data.

            Returns precomputed aggregates (300 seconds) for session 'id'.
            In case of interesting event during the interval (packet loss)
            higher resolution data is returned for an interval around the 
            interesting event.
        """

        start = ((int(time.time()) / self.AGGR_DB_LOWRES - num - 1) * self.AGGR_DB_LOWRES - self.HIGHRES_INTERVAL) * 1000000000
        self.logger.debug("Getting last_dyn_aggregate for id %d from %d now: %d" % (session_id, start/1000000000, int(time.time())))
        sql = ("SELECT * FROM probes_aggregate WHERE session_id = ? AND " +
            "created >= ? AND aggr_interval = ?")
        res = self.db.select(sql, (session_id, start, self.AGGR_DB_LOWRES))

        ret = list()
        for row in res:
            ret.append(dict(row))

        return ret

    
    def get_last_dyn_aggregate(self, session_id, num = 1):
        """ Get last precomputed aggregate data.

            Returns precomputed aggregates (300 seconds) for session 'id'.
            In case of interesting event during the interval (packet loss)
            higher resolution data is returned for an interval around the 
            interesting event.
            Zib-delux funktion.
        """
        # the +10 is for ASM that always starts asking 00:00:00 etc, and
        # therefore it's a risk that we "averages" the time into the same 
        # 5-min interval two times in a row. for example:
        # requests 00:40:00 and 00:44:59 = will end up in the same interval.
        # That's what makes this API (get_last_dyn_aggregate) so bad. Crappy
        # crappy crappy :)
        start = ((int(time.time() + 10) / self.AGGR_DB_LOWRES - num - 1) * self.AGGR_DB_LOWRES) * 1000000000
        end =   ((int(time.time() + 10) / self.AGGR_DB_LOWRES - 1)       * self.AGGR_DB_LOWRES) * 1000000000
        self.logger.debug("Getting last_dyn_aggregate for id %d from %d now: %d" % (session_id, start/1000000000, int(time.time())))
        sql = ("SELECT * FROM probes_aggregate WHERE session_id = ? AND " +
            "created >= ? AND created < ?")
        res = self.db.select(sql, (session_id, start, end))

        ret = list()
        for row in res:
            ret.append(dict(row))

        return ret


    def get_aggregate(self, session_id, start, end):
        """ Get round-trip time

            Get average round-trip time for measurement session session_id
            whose probes were created as end < created <= end.
        """

        retdata = {
            'total': 0,
            'success': 0,
            'timestamperror': 0,
            'pongloss': 0,
            'dscperror': 0,
            'timeout': 0,
            'dup': 0,
            'reordered': 0,
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
        sql = "SELECT CAST( COUNT(*) AS REAL) AS c FROM PROBES WHERE state = ? AND " + where
        res = self.db.select(sql, (probe.STATE_TIMEOUT, ) + whereargs)
        for row in res:
            retdata['timeout'] = row['c']

        sql = "SELECT CAST( COUNT(*) AS REAL) AS c FROM PROBES WHERE state = ? AND " + where
        res = self.db.select(sql, (probe.STATE_OK, ) + whereargs)
        for row in res:
            retdata['success'] = row['c']

        sql = "SELECT CAST( COUNT(*) AS REAL) AS c FROM PROBES WHERE state = ? AND " + where
        res = self.db.select(sql, (probe.STATE_DUP, ) + whereargs)
        for row in res:
            retdata['dup'] = row['c']

        sql = "SELECT CAST( COUNT(*) AS REAL) AS c FROM PROBES WHERE state = ? AND " + where
        res = self.db.select(sql, (probe.STATE_TSERROR, ) + whereargs)
        for row in res:
            retdata['timestamperror'] = row['c']

        sql = "SELECT CAST( COUNT(*) AS REAL) AS c FROM PROBES WHERE state = ? AND " + where
        res = self.db.select(sql, (probe.STATE_PONGLOSS, ) + whereargs)
        for row in res:
            retdata['pongloss'] = row['c']

        sql = "SELECT CAST( COUNT(*) AS REAL) AS c FROM PROBES WHERE state = ? AND " + where
        res = self.db.select(sql, (probe.STATE_DSERROR, ) + whereargs)
        for row in res:
            retdata['dscperror'] = row['c']

        sql = "SELECT CAST( COUNT(*) AS REAL) AS c FROM PROBES WHERE in_order != 1 AND " + where
        res = self.db.select(sql, whereargs)
        for row in res:
            retdata['reordered'] = row['c']

        where = "session_id = ? AND created >= ? AND created < ? AND (state = ?  OR state = ? )"
        whereargs = (session_id, start, end, probe.STATE_OK, probe.STATE_DSERROR)
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

        where = "session_id = ? AND created >= ? AND created < ? AND (state = ?  OR state = ?)"
        whereargs = (session_id, start, end, probe.STATE_OK, probe.STATE_DSERROR)

        sql = "SELECT COUNT(*) AS count FROM probes WHERE " + where
        res = self.db.select(sql, whereargs)
        nrows = 0
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
        """ Get storage statistics """

        retval = {}

        # Get number of rows in database
        sql = "SELECT COUNT(*) AS c FROM probes"
        res = self.db.select(sql)
        for row in res:
            retval['db_numrows_probes'] = row['c']

        sql = "SELECT COUNT(*) AS c FROM probes_aggregate"
        res = self.db.select(sql)
        for row in res:
            retval['db_numrows_probes_aggregate'] = row['c']
        
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
                self.logger.debug("Committing %d queries." % exec_c)
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

    def execute(self, req, arg = None, res = None):
        """ "Execute" by pushing query to queue """

        self.reqs.put((req, arg or tuple(), res))

    def select(self, req, arg = None):
        res = Queue()
        self.execute(req, arg, res)
        while True:
            rec = res.get()
            if rec == '--no more--': break
            yield rec

    def close(self):
        self.logger.debug("Got close request, passing to thread.")
        self.execute('--close--')

    def commit(self):
        self.execute('--commit--')
