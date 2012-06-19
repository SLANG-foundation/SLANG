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


    def xmlrpc_get_last_highres(self, session_id, num=1):
        """ Get latest finished high-resolution data.

        Arguments:
        session_id -- ID of the session data is requested for.
        num -- Number of values to return -- NOT IMPLEMENTED

        """

        res = self.pstore.get_last_highres(session_id, num)
        for row in res:
            # Clean up timestamps.
            row['start'] = row['created'] / 1000000000
            del row['created']
            for k, v in row.items():
                if row[k] is not None:
                    row[k] = str(v)

        return res

    xmlrpc_get_last_highres.signature = [ ['array', 'integer', 'integer'], ]

    def xmlrpc_get_last_lowres(self, session_id, num = 1):
        """ Get latest finished low-resolution data.

        Arguments:
        session_id -- ID of the session data is requested for.
        num -- Number of values to return -- NOT IMPLEMENTED

        """

        data = self.pstore.get_last_lowres(session_id, num)

        for row in data:
            # Clean up timestamps.
            row['start'] = row['created'] / 1000000000
            del row['created']
            for k, v in row.items():
                if row[k] is not None:
                    row[k] = str(v)

        return data

    xmlrpc_get_last_lowres.signature = [ ['array', 'integer', 'integer'], ]

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
            row['start'] = row['created'] / 1000000000
            del row['created']
            for k, v in row.items():
                if row[k] is not None:
                    row[k] = str(v)

        return data

    xmlrpc_get_last_dyn_aggregate.signature = [ [ 'array', 'integer', 'integer' ], ]


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
