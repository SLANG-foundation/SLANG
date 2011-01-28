from twisted.web import xmlrpc, server

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

    def xmlrpc_get_raw(self, session_id, start, end):
        """ Get raw data.

        Get raw measurement data for a measurement session.
        Arguments:
        session_id -- ID of the session data is requested for.
        start -- Start time.
        stop -- End time.

        """

        return self.pstore.get_raw(session_id, start, end)
