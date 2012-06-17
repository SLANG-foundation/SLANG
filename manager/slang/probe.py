from struct import unpack
import time

#
# constants
#
STATE_OK = 1       # Ready, got both PONG and valid TS
STATE_DSERROR = 2  # Ready, but invalid traffic class
STATE_TSERROR = 3  # Ready, but missing timestamps
STATE_PONGLOSS = 4 # Ready, but timeout, got only TS, lost PONG
STATE_TIMEOUT = 5  # Ready, but timeout, got neither PONG or TS
STATE_DUP = 6      # Got a PONG we didn't recognize, DUP?

def from_struct(structdata):
    """ Create a probe from the struct data """
    d = unpack('iiiiiii', structdata)

    clist = (
        d[0],
        d[1],
        d[2],
        d[3]*1000000000+d[4],
        d[5]*1000000000+d[6],
    )

    return Probe(clist) 

class Probe:
    """ A probe. """

    created = None
    rtt = None
    delay_variation = None
    in_order = None
    dups = None
    state = None

    session_id = None
    seq = None

    has_given = None
    has_gotten = None

    def __init__(self, data):
        self.session_id = data[0]
        self.seq = data[1]
        self.state = data[2]
        self.created = data[3]
        self.rtt = data[4]
        self.in_order = None
        self.delay_variation = None
        self.dups = 0

        self.has_given = False
        self.has_gotten = False

    def __str__(self):
        if self.state == STATE_OK:
            return str('Response %5d from %d in %d ns' % 
                (self.seq, self.session_id, self.rtt))
        if self.state == STATE_DSERROR:
            return str('Error    %5d from %d in %d ns (invalid DSCP)' % 
                (self.seq, self.session_id, self.rtt))
        if self.state == STATE_TSERROR:
            return str('Error    %5d from %d (missing T2/T3)' % 
                (self.seq, self.session_id))
        if self.state == STATE_PONGLOSS:
            return str('Timeout  %5d from %d (missing PONG)' % 
                (self.seq, self.session_id))
        if self.state == STATE_TIMEOUT:
            return str('Timeout  %5d from %d (missing all)' % 
                (self.seq, self.session_id))
        if self.state == STATE_DUP:
            return str('Unknown  %5d from %d (probably DUP)' % 
                (self.seq, self.session_id))
        return 'Unable to parse ' + str(self.state)

    def toDict(self):
        """ Returns data as a dict.

            Creates a dict with data types suitable for XML-RPC.
        """

        return {
            'session_id': self.session_id,
            'seq': self.seq,
            'state': self.state,
            'in_order': self.in_order,
            'created': self.created,
            'rtt': self.rtt,
            'delayvar': self.delay_variation,
            'dups': self.dups,
            'has_given': self.has_given,
            'has_gotten': self.has_gotten
        }


    def lost(self):
        """ Was the packet lost? """
        
        return self.state == STATE_TIMEOUT


    def successful(self):
        """ Do we have all timestamp? """

        return self.state == STATE_OK or self.state == STATE_DSERROR


    def set_prev_probe(self, prev_probe):
        """ Perform calculations which require previous probe.
        """
        try:
            if self.successful() and prev_probe.successful():
                self.delay_variation = self.rtt - prev_probe.rtt
            else:
                self.delay_variation = None
        except TypeError:
            # catch error when RTT is None
            pass

        self.has_gotten = True
        prev_probe.has_given = True
