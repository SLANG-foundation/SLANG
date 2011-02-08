from twisted.web import xmlrpc, server
import logging
import time

class RemoteProc(xmlrpc.XMLRPC):
    """ Remote procedures """

    pstore = None
    manager = None
    logging = None

    def __init__(self, pstore, manager):
        """ Constructor """
        xmlrpc.XMLRPC.__init__(self)

        self.logger = logging.getLogger(self.__class__.__name__)
        self.manager = manager
        self.pstore = pstore

    def xmlrpc_reload(self):
        """ Reload application """

        self.manager.reload()

    def xmlrpc_get_aggregate(self, session_id, aggr_interval, start, end):
        """ Get aggregated data.

        Get aggregated measurement data for a measurement session.
        Arguments:
        session_id -- ID of the session data is requested for.
        atime -- Aggregation time, the number of seconds results will be aggregated.
        start -- Start time.
        stop -- End time.

        """

        result = list()

        # get times
        ctime = start
        aggr_times = list()
        while ctime < end:
            aggr_times.append(ctime)
            ctime += aggr_interval
        aggr_times.append(end)

        for i in range(0, len(aggr_times) - 1):
#            t = time.time()
            r = self.pstore.get_aggregate(session_id, aggr_times[i]*1000000000, aggr_times[i+1]*1000000000)
            r['start'] = aggr_times[i]

            # set null values to zero before we send them over XML-RPC
            for k, v in r.items():
                if v == None:
                    r[k] = 0

            result.append(r)
#            print "iteration %d finished in %f seconds" % (i, time.time() - t)

#        for a in result:
#            self.logger.debug("Aggregated data:")
#            for k, v in a.items():
#                self.logger.debug("%s: %s" % (k, v))

        return result

    def xmlrpc_get_raw(self, session_id, start, end):
        """ Get raw data.

        Get raw measurement data for a measurement session.
        Arguments:
        session_id -- ID of the session data is requested for.
        start -- Start time.
        stop -- End time.

        """

        pset = self.pstore.get_raw(session_id, start, end)

        pd_list = []
        for p in pset:
            pdlist.append(p.toDict)

        return pd_list
    
    def xmlrpc_get_raw_interval(self, session_id, start = 0, end = 300):
        """ Get raw data within a time interval

        Get raw measurement data for a measurement session.
        Arguments:
        session_id -- ID of the session data is requested for.
        start -- The number of seconds from 'now'.
        stop -- The length of the interval, in seconds.

        """
        start = time.time() - start;
        end = time.time() - end;

        pset = self.pstore.get_raw(session_id, start, end)
        
        pd_list = []
        for p in pset:
            pdlist.append(p.toDict)

        return pd_list

    def xmlrpc_get_current_sessions(self):
        """ Get current measurement sessions there is data for. """

        return self.pstore.current_sessions()

    def xmlrpc_get_storage_statistics(self):
        """ Get some statistics from the probe storage. """
        
        return self.pstore.get_storage_statistics()
