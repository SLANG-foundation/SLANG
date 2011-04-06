from twisted.web import xmlrpc, server
import logging
import time
import xmlrpclib

import manager

class RemoteProc(xmlrpc.XMLRPC):
    """ Remote procedures """

    allow_none = True
    pstore = None
    manager = None
    logging = None

    def __init__(self, pstore, manager):
        """ Constructor """
        xmlrpc.XMLRPC.__init__(self)
        self.allowNone = True

        self.logger = logging.getLogger(self.__class__.__name__)
        self.manager = manager
        self.pstore = pstore

    def xmlrpc_reload(self):
        """ Reload application """

        try:
            self.manager.reload()
        except manager.ManagerError, e:
            return xmlrpclib.Fault(1100, str(e))

        return True

    xmlrpc_reload.signature = [ ['integer', ], ]


    def xmlrpc_recv_config(self, cfg):
        """ Receive a new config, reload application. """

        try:
            self.manager.write_config(cfg)
        except manager.ManagerError, e:
            return xmlrpclib.Fault(1100, str(e))

        return True

    xmlrpc_recv_config.signature = [ [ 'integer', 'string' ], ]


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
            r = self.pstore.get_aggregate(session_id, 
                aggr_times[i]*1000000000, aggr_times[i+1]*1000000000)
            r['start'] = aggr_times[i]

            # set null values to zero before we send them over XML-RPC
            for k, v in r.items():
                if v == None:
                    r[k] = str(0)
                else:
                    r[k] = str(v)

            result.append(r)

        return result

    xmlrpc_get_aggregate.signature = [ ['array', 'integer', 'integer', 'integer', 'integer'], ]

    def xmlrpc_get_last_lowres_aggregate(self, session_id, num = 1):
        """ Get last precomputed aggregate data.

            Returns precomputed aggregates (300 seconds) for session 'id'.
            In case of interesting event during the interval (values higher 
            than the baseline), higher resolution data is returned for an 
            interval around the interesting event.
        """

        data = self.pstore.get_last_lowres_aggregate(session_id, num)

        for row in data:
            # Clean up timestamps.
            r = row
            r['start'] = r['created'] / 1000000000
            del r['created']
            # set null values to zero before we send them over XML-RPC
            for k, v in r.items():
                if v == None:
                    r[k] = str(0)
                else:
                    r[k] = str(v)

        return data


    def xmlrpc_get_last_dyn_aggregate(self, session_id, num = 1):
        """ Get last precomputed aggregate data.

            Returns precomputed aggregates (300 seconds) for session 'id'.
            In case of interesting event during the interval (values higher 
            than the baseline), higher resolution data is returned for an 
            interval around the interesting event.
        """

        data = self.pstore.get_last_dyn_aggregate(session_id, num)

        for row in data:
            # Clean up timestamps.
            r = row
            r['start'] = r['created'] / 1000000000
            del r['created']
            # set null values to zero before we send them over XML-RPC
            for k, v in r.items():
                if v == None:
                    r[k] = str(0)
                else:
                    r[k] = str(v)

        return data

    xmlrpc_get_last_dyn_aggregate.signature = [ [ 'array', 'integer' ], ]
    

    def xmlrpc_get_current_sessions(self):
        """ Get current measurement sessions there is data for. """

        return self.pstore.current_sessions()

    xmlrpc_get_current_sessions.signature = [ [ 'array' ], ]


    def xmlrpc_get_storage_statistics(self):
        """ Get some statistics from the probe storage. """
        
        return self.pstore.get_storage_statistics()

    xmlrpc_get_storage_statistics.signature = [ [ 'struct' ], ]

    def xmlrpc_flush_queue(self):
        """ Flush probe queue. """

        self.pstore.flush_queue()
        return True

    xmlrpc_get_storage_statistics.signature = [ [ 'boolean' ], ]
