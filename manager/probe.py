from struct import unpack
from timespec import Timespec

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
    msess_id = None
    seq = None

    def __init__(self, data):

        self.created = Timespec(data[0], data[1])
        self.state = data[2]
        self.addr = data[3]
        self.msess_id = data[4]
        self.seq = data[5]
        self.t1 = Timespec(data[6], data[7])
        self.t2 = Timespec(data[8], data[9])
        self.t3 = Timespec(data[10], data[11])
        self.t4 = Timespec(data[12], data[13])

    def rtt(self):
        """ Calculates the rtt of the probe. """

        return (self.t4 - self.t1) - (self.t3 - self.t2)

class ProbeSet(list):
    """ A set of probes. """

    def agg(self, agg = None):
        """ Return aggregated data as ... """

    def get_agg_data(self, agg_interval):
        """ Get suitable data aggregated with interval agg_interval """

        idxlist = self.get_agg_indices(agg_interval)

        res = []

        for idx in idxlist:
            subset = ProbeSet(self[idx[0]:idx[1]])
            subdata = {
                'max_rtt': subset.max_rtt(),
                'min_rtt': subset.min_rtt(),
                'avg_rtt': subset.avg_rtt(),
                'mean_rtt': subset.perc_rtt(50),
                '80perc_': subset.perc_rtt(80),
            }
            res.append(subdata)

        return res

    def get_agg_indices(self, agg_interval):
        """ Get indices for a certain aggregation interval """

        idx = []
        cur_start_time = self[0].created
        cur_start_idx = 0
        
        for i in range(0, len(self)-1):
            if self[i].created > cur_start_time + agg_interval:
                idx.append( (cur_start_idx, i-1) )
                cur_start_idx = i
                cur_start_time += agg_interval

        idx.append( (cur_start_idx, len(self)-1) )

        return idx


    def avg_rtt(self):
        """ Find the average RTT """
        
        sum = Timespec(0, 0)
        for p in self:
            sum += p.rtt()

        return sum/len(self)

    def max_rtt(self):
        """ Find the max RTT """
        r = []
        for p in self:
            r.append(p.rtt())
        return max(r)

    def min_rtt(self):
        """ Find the min RTT """
        r = []
        for p in self:
            r.append(p.rtt())
        return min(r)

    def perc_rtt(self, perc):
        """ Find the percth percentile """

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
        
        return r[ int( round( perc * 0.01 * (len(r)-1) ) ) ]
