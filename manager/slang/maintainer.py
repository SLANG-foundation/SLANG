import threading
from time import time, sleep
import logging
import resource

import config

class Maintainer(threading.Thread):
    """ Performs maintenance operations at regular intervals. """

    flush_interval = 1
    delete_interval = 600
    reload_interval = 3600

    logger = None
    pstore = None
    manager = None
    thread_stop = False

    last_flush = None
    last_delete = None
    last_reload = None

    def __init__(self, pstore, manager):

        threading.Thread.__init__(self, name=self.__class__.__name__)

        self.logger = logging.getLogger(self.__class__.__name__)
        self.config = config.Config()
        self.manager = manager

        # save ProbeStorage instance
        self.pstore = pstore

        # initialize time tracking instance variables
        self.last_flush = time()
        self.last_delete = time()
        self.last_reload = time()
        self.last_pstore_aggr = ((int(time() * 1000000000) / self.pstore.AGGR_DB_LOWRES) *
            self.pstore.AGGR_DB_LOWRES + 2*self.pstore.AGGR_DB_LOWRES)


    def run(self):
        """ Starts thread. """

        self.logger.debug("Starting thread")

        while True:

            if self.thread_stop:
                break

            t_s = time()

            # flush lowres aggregates to highres aggregates
            # If we are behind, run many times!
            while (t_s - self.last_flush) >= self.flush_interval:

                ctime = self.last_flush + 1
                self.pstore.flush(ctime)

                self.last_flush = ctime

            # remove old data from database, 30min HIGHRES and 24h LOWRES
            if (t_s - self.last_delete) >= self.delete_interval:
                self.pstore.delete(1800, 3600*24)
                self.last_delete = t_s

            # reload every hour
            if (t_s - self.last_reload) >= self.reload_interval:
                try:
                    self.manager.reload()
                except Exception, e:
                    self.logger.error("Maintenace reload operation failed: %s"
                        % str(e))
                self.last_reload = t_s

            # save precalculated aggregates to database
            #
            # run when we are certain that we have all data for previous
            # aggr_db_lowres
            if (t_s * 1000000000 > (self.last_pstore_aggr + self.pstore.AGGR_DB_LOWRES + self.pstore.HIGHRES_INTERVAL + self.pstore.TIMEOUT + 2*1000000000)):

                t_start = t_s
                # the previous lowres interval
                age = self.last_pstore_aggr

                self.logger.debug("Aggregating; current lowres interval: %d will aggregate: %d current time: %d" % (((int(t_start * 1000000000) / self.pstore.AGGR_DB_LOWRES) * self.pstore.AGGR_DB_LOWRES) / 1000000000, age/1000000000, int(t_start) ))

                try:
                    self.pstore.aggregate(age)
                except Exception, e:
                    self.logger.error("aggregation failed with %s: %s. Missing valid measurement data?" % (str(type(e)), str(e)))

                self.last_pstore_aggr = ((int(t_start * 1000000000) / self.pstore.AGGR_DB_LOWRES) * self.pstore.AGGR_DB_LOWRES)

                t_stop = time()
                self.logger.debug("Aggregation performed in %.3f seconds" % (t_stop - t_start))


            self.logger.debug("thread %d run time: %f s" % (self.ident, resource.getrusage(1)[0]))
            self.manager.run_stats()

            sleep(1)


    def stop(self):
        """ Stop thread """

        self.thread_stop = True
