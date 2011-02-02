from twisted.web import xmlrpc, server
import logging

from timespec import Timespec

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

        self.logger.debug("Getting full ProbeSet")
        full_pset = self.pstore.get_raw(session_id, start, end)
        self.logger.debug("Done. Splitting ProbeSet")
        s_pset = full_pset.split(Timespec(aggr_interval, 0))
        self.logger.debug("Done. Computing aggregates")
        
        res = []
        for pset in s_pset:
            self.logger.debug("Subset of %d probes." % len(pset))
            res.append({
                'created': pset[0].created,
                'max_rtt': pset.max_rtt(),
                'min_rtt': pset.min_rtt(),
                'avg_rtt': pset.avg_rtt(),
                'mean_rtt': pset.perc_rtt(50),
                '95perc_rtt': pset.perc_rtt(95),
                'perc_lost': pset.lost()/len(pset)
            })

        self.logger.debug("Done computing aggregates")

        return res

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
