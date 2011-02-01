from twisted.web import xmlrpc, server
import time

class RemoteProc(xmlrpc.XMLRPC):
    """ Remote procedures """

    pstore = None
    manager = None
    logging = None

    def __init__(self, pstore, manager):
        """ Constructor """
        xmlrpc.XMLRPC.__init__(self)

        self.manager = manager
        self.pstore = pstore

    def xmlrpc_reload(self):
        """ Reload application """

        self.manager.reload()

    def xmlrpc_get_aggregate(self, session_id, atime, start, end=None):
        """ Get aggregated data.

        Get aggregated measurement data for a measurement session.
        Arguments:
        session_id -- ID of the session data is requested for.
        atime -- Aggregation time, the number of seconds results will be aggregated.
        start -- Start time.
        stop -- End time.

        """

        return 1

    def xmlrpc_get_raw(self, session_id, start, end=None):
        """ Get raw data.

        Get raw measurement data for a measurement session.
        Arguments:
        session_id -- ID of the session data is requested for.
        start -- Start time.
        stop -- End time.

        """

        return self.pstore.get_raw(session_id, start, end)
    
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
        
        return self.pstore.get_raw(session_id, start, end)
