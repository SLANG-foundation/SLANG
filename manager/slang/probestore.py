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
    l_probe_raw_lowres = None
    probe_raw_lowres = None
    l_probe_raw_highres = None
    probe_raw_highres = None
    probe_raw_highres_backlog = None

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
        self.probe_raw_lowres = dict()
        self.l_probe_raw_lowres = threading.Lock()
        self.probe_raw_highres = dict()
        self.l_probe_raw_highres = threading.Lock()
        self.probe_raw_highres_backlog = dict()

        self.logger.debug("created instance")

        # open database connection
        try:
            self.db = ProbeStoreDB(self.config.get('dbpath'))

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
        """ Insert probe into aggregated storage. """

        # acquire lock
        self.l_probe_raw_highres.acquire(True)

        # session in storage?
        if p.session_id not in self.probe_raw_highres:
            self.probe_raw_highres[p.session_id] = {
                'created': int(p.created / 1000000000 / self.AGGR_DB_HIGHRES) * self.AGGR_DB_HIGHRES * 1000000000,
                'rtts': list(),
                'delayvars': list(),
                'total': 0,
                'success': 0,
                'timestamperror': 0,
                'pongloss': 0,
                'dscperror': 0,
                'timeout': 0,
                'dup': 0,
                'reordered': 0
            }

        # if successful, update rtt & delayvar
        if p.successful:
            if p.rtt is not None:
                self.probe_raw_highres[p.session_id]['rtts'].append(p.rtt)
            if p.delay_variation is not None:
                self.probe_raw_highres[p.session_id]['delayvars'].append(abs(p.delay_variation))

        # update state counters
        if p.state == probe.STATE_OK:
            self.probe_raw_highres[p.session_id]['success'] +=1
        elif p.state == probe.STATE_DSERROR:
            self.probe_raw_highres[p.session_id]['dscperror'] += 1
        elif p.state == probe.STATE_TSERROR:
            self.probe_raw_highres[p.session_id]['timestamperror'] += 1
        elif p.state == probe.STATE_PONGLOSS:
            self.probe_raw_highres[p.session_id]['pongloss'] += 1
        elif p.state == probe.STATE_TIMEOUT:
            self.probe_raw_highres[p.session_id]['timeout'] += 1

        self.probe_raw_highres[p.session_id]['total'] += 1
        self.probe_raw_highres[p.session_id]['dup'] += bool(p.dups)

        # release lock
        self.l_probe_raw_highres.release()


    def flush(self):
        """ Flush high-res data. """

        # acquire lock
        self.l_probe_raw_highres.acquire(True)

        # copy & empty dict before performing work
        tmp_highres = self.probe_raw_highres
        self.probe_raw_highres = {}

        # release lock
        self.l_probe_raw_highres.release()

        if len(tmp_highres) == 0:
            self.logger.debug("No data to flush.")
            return
    
        ctime = tmp_highres[ tmp_highres.keys()[0] ]['created']
        self.logger.debug("Flushing one-second data for %d sessions, time %d" % 
            (len(tmp_highres), int(ctime/1000000000)))

        # add to five min aggregate
        for session_id in tmp_highres:

            if session_id not in self.probe_raw_highres:
                self.probe_raw_lowres[session_id] = {
                    'created': int(tmp_highres[session_id]['created'] / 
                        1000000000 / self.AGGR_DB_LOWRES) * 
                        self.AGGR_DB_LOWRES * 1000000000,
                    'rtts': list(),
                    'delayvars': list(),
                    'total': 0,
                    'success': 0,
                    'timestamperror': 0,
                    'pongloss': 0,
                    'dscperror': 0,
                    'timeout': 0,
                    'dup': 0,
                    'reordered': 0 }

            # save highres data to backlog
            if ctime not in self.probe_raw_highres_backlog:
                self.probe_raw_highres_backlog[ctime] = {}
            
            self.probe_raw_highres_backlog[ctime][session_id] = tmp_highres[session_id]

            # perform addition of all fields
            for key in self.probe_raw_lowres[session_id]:
                if key == 'created':
                    continue

                self.probe_raw_lowres[session_id][key] += tmp_highres[session_id][key]

        # remove old data from backlog
        to_del = list()
        for t in self.probe_raw_highres_backlog:
            if t + 2 * self.HIGHRES_INTERVAL * 1000000000 < ctime:
                to_del.append(t)

        for t in to_del:
            self.logger.debug("Removing values of age %d from backlog" % int(t/1000000000))
            del(self.probe_raw_highres_backlog[t])

        #
        # check backlog - are there interesting events?
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
        action_time = ctime - self.HIGHRES_INTERVAL * 1000000000
        if action_time not in self.probe_raw_highres_backlog:
            # missing data...
            return
        
        for session_id in self.probe_raw_highres_backlog[action_time]:
            
            # check packet loss
            if (self.probe_raw_highres_backlog[action_time][session_id]['timeout'] > 0 or
                self.probe_raw_highres_backlog[action_time][session_id]['pongloss'] > 0):
                
                # Action! Save highres data.
                for t in self.probe_raw_highres_backlog:
                    self.calc_probedict(self.probe_raw_highres_backlog[t][session_id])
                    self.save(session_id, self.AGGR_DB_HIGHRES, self.probe_raw_highres_backlog[t][session_id])


    def calc_probedict(self, p):
        """ Calculate values such as min, max and mean for a probe dict """

        # sort rtt & delayvar lists
        p['delayvars'].sort()
        p['rtts'].sort()
       
        # find max, min, mean and 95:th
        if len(p['rtts']) == 0:
            p['rtt_max'] = None
            p['rtt_min'] = None
            p['rtt_avg'] = None
            p['rtt_med'] = None
            p['rtt_95th'] = None
        else:
            p['rtt_max'] = p['rtts'][-1]
            p['rtt_min'] = p['rtts'][0]
            p['rtt_avg'] = float(sum(p['rtts'])) / len(p['rtts'])
            p['rtt_med'] = p['rtts'][int(len(p['rtts']) * 0.5)]
            p['rtt_95th'] = p['rtts'][int(len(p['rtts']) * 0.95)]

        if len(p['delayvars']) == 0:
            p['delayvar_max'] = None
            p['delayvar_min'] = None
            p['delayvar_avg'] = None
            p['delayvar_med'] = None
            p['delayvar_95th'] = None
        else:
            p['delayvar_max'] = p['delayvars'][-1]
            p['delayvar_min'] = p['delayvars'][0]
            p['delayvar_avg'] = float(sum(p['delayvars'])) / len(p['delayvars'])
            p['delayvar_med'] = p['delayvars'][int(len(p['delayvars']) * 0.5)]
            p['delayvar_95th'] = p['delayvars'][int(len(p['delayvars']) * 0.95)]


    def delete(self, age, age_lowres):
        """ Deletes saved data of age 'age' and older. """
    
        self.logger.info("Deleting old data from database.")

        now = int(time.time())

        sql = "DELETE FROM probes_aggregate WHERE created < ? AND aggr_interval = ?"
        self.db.execute(sql, ((now - age)*1000000000, self.AGGR_DB_HIGHRES))

        sql = "DELETE FROM probes_aggregate WHERE created < ?"
        self.db.execute(sql, ((now - age_lowres)*1000000000, ))


    def save(self, sess_id, interval, data):
        """ Save an aggregate to database. 
        
            The aggregate is passed as a dict with the required fields; 
            check the code!
        """

        self.logger.debug("Saving row; sess_id: %d interval: %d rtt_avg: %.2f ns" 
            % (sess_id, interval, data['rtt_avg']))

        # insert into database
        sql = ("INSERT INTO probes_aggregate " +
            "(session_id, aggr_interval, created, total, " +
            "success, timestamperror, dscperror, pongloss, timeout, " +
            "dup, reordered, rtt_min, rtt_med, rtt_avg, rtt_max, " +
            "rtt_95th, delayvar_min, delayvar_med, delayvar_avg, " +
            "delayvar_max, delayvar_95th) VALUES " +
            "(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ? )")

        params = (
            sess_id, interval, data['created'], data['total'], data['success'],
            data['timestamperror'], data['dscperror'], data['pongloss'], data['timeout'], 
            data['dup'], data['reordered'], data['rtt_min'], data['rtt_med'], data['rtt_avg'], 
            data['rtt_max'], data['rtt_95th'], data['delayvar_min'], 
            data['delayvar_med'], data['delayvar_avg'], data['delayvar_max'], 
            data['delayvar_95th'])

        self.db.execute(sql, params)


    def aggregate(self, sess_id):
        """ Aggregates self.AGGR_DB_LOWRES seconds worth of data. 
        
            Starts att time 'start'. The result will be written to the
            database.
        """

        self.logger.debug("aggregating session %d, %d seconds interval" % 
            (sess_id, self.AGGR_DB_LOWRES))

        # get aggregated data
        try:
            self.calc_probedict(self.probe_raw_lowres[sess_id])
            data = self.probe_raw_lowres[sess_id]
        except KeyError, e:
            self.logger.error("Unable to aggregate session %d, not found" % sess_id)
            return

        self.save(sess_id, self.AGGR_DB_LOWRES, data)
        del(self.probe_raw_lowres[sess_id])



    def commit(self):
        """ Commit database transactions. """

        self.db.commit()


    ###################################################################
    #
    # Functions to fetch data from database
    #

    def current_sessions(self):
        """ Get IDs of current sessions """

        return self.probe_raw_lowres.keys()


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


    def get_storage_statistics(self):
        """ Return some storage statistics. """

        ret = {}
        ret['lowres'] = len(self.probe_raw_lowres)
        ret['highres'] = len(self.probe_raw_highres)

        ret['highres_backlog'] = 0
        for t in self.probe_raw_highres_backlog:
            ret['highres_backlog'] += len(self.probe_raw_highres_backlog[t])

        res = self.db.select("SELECT COUNT(*) AS c FROM probes_aggregate")
        for row in res:
            ret['aggregates'] = row['c']

        return ret


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
                self.logger.debug("Committed %d queries (%s queries queued)." % (exec_c, self.reqs.qsize()))
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
