from struct import unpack
from timespec import Timespec
import time

#
# constants
#
STATE_PING = 'i'
STATE_GOT_TS = 't' # Because of Intel RX timestamp bug 
STATE_GOT_PONG = 'o' # Because of Intel RX timestamp bug 
STATE_READY = 'r'
STATE_TSERROR = 'e' # Missing timestamp (Intel...?)
STATE_TIMEOUT = 't'

def from_struct(structdata):
    """ Create a probe from the struct data """
    
    listdata = unpack('llc16siillllllll16s', structdata)
    return Probe(listdata) 

class Probe:
    """ A probe. """

    created = None
    t1 = None
    t2 = None
    t3 = None
    t4 = None

    addr = None
    session_id = None
    seq = None

    def __init__(self, data):

        self.created = Timespec(data[0], data[1])
        self.state = data[2]
        self.addr = data[3]
        self.session_id = data[4]
        self.seq = data[5]
        self.t1 = Timespec(data[6], data[7])
        self.t2 = Timespec(data[8], data[9])
        self.t3 = Timespec(data[10], data[11])
        self.t4 = Timespec(data[12], data[13])

    def rtt(self):
        """ Calculates the rtt of the probe. """

        return (self.t4 - self.t1) - (self.t3 - self.t2)

    def toDict(self):
        """ Returns data as a dict.

            Creates a dict with data types suitable for XML-RPC.
        """

        return {
            'session_id': self.session_id,
            'seq': self.seq,
            'state': self.state,
            'created': (self.created.sec, self.created.nsec),
            't1': (self.t1.sec, self.t1.nsec),
            't2': (self.t2.sec, self.t2.nsec),
            't3': (self.t3.sec, self.t3.nsec),
            't4': (self.t4.sec, self.t4.nsec)
        }

    def lost(self):
        """ Was the packet lost? """
        
        return not (self.state == STATE_READY or self.state == STATE_TSERROR)

class ProbeSet(list):
    """ A set of probes. """


    def split(self, split_interval):
        """ Split up the ProbeSet 
        
            Splits up the ProbeSet into a list of smaller ProbeSets,
            each one containing Probes for a split_interval.

            TODO: move slicing code into the index findning loop
        """

        # Find indexes
        idxlist = []
        cur_start_time = self[0].created
        cur_start_idx = 0
        
        for i in range(0, len(self)-1):
            if self[i].created > cur_start_time + split_interval:
                idxlist.append( (cur_start_idx, i) )
                cur_start_idx = i
                cur_start_time += split_interval

        idxlist.append( (cur_start_idx, len(self)) )

        # Iterate index list and slice array
        res = []
        for idx in idxlist:
            subset = ProbeSet(self[idx[0]:idx[1]])
            res.append(subset)

        return res


    def get_aggr_indices(self, agg_interval):
        """ Get indices for a certain aggregation interval """


        return idx


    def avg_rtt(self):
        """ Find the average RTT """
        t = time.time()
        sum = Timespec(0, 0)
        for p in self:
            sum += p.rtt()

        print "avg_rtt: %f" % (time.time() - t)
        return sum/len(self)


    def max_rtt(self):
        """ Find the max RTT """
        t = time.time()
        r = []
        for p in self:
            r.append(p.rtt())
        print "max_rtt: %f" % (time.time() - t)
        return max(r)


    def min_rtt(self):
        """ Find the min RTT """
        t = time.time()
        r = []
        for p in self:
            r.append(p.rtt())
        print "min_rtt: %f" % (time.time() - t)
        return min(r)


    def perc_rtt(self, perc):
        """ Find the percth percentile """

        t = time.time()

        # sanity check of percentile
        if perc < 0 or perc > 100:
            raise Exception("Invalid percentile")

        # get rtt:s
        r = []
        for p in self:
            r.append(p.rtt())

        r.sort()

        # special cases for "strange" percentiles
        if perc == 0:
            return r[0]
        if perc == 100:
            return r[-1]
        
        print "perc_rtt: %f" % (time.time() - t)

        return r[ int( round( perc * 0.01 * (len(r)-1) ) ) ]

    def lost(self):
        """ Return number of lost probes """
        
        t = time.time()
        l = 0
        for p in self:
            if p.lost(): l +=1

        print "perc_lost: %f" % (time.time() - t)

        return l



