from struct import unpack
import time

#
# constants
#
STATE_OK = 'o'       # Ready, got both PONG and valid TS */ 
STATE_TSERROR = 'e'  # Ready, but missing correct TS */ 
STATE_DSERROR = 'd'  # Ready, but invalid traffic class */ 
STATE_PONGLOSS = 'l' # Ready, but timeout, got only TS, lost PONG */ 
STATE_TIMEOUT = 't'  # Ready, but timeout, got neither PONG or TS */ 
STATE_DUP = 'u'      # Got a PONG we didn't recognize, DUP? */ 

def from_struct(structdata):
    """ Create a probe from the struct data """
    
    listdata = unpack('llc16siillllllll16s', structdata)

    clist = (
      listdata[0]*1000000000+listdata[1],   # 0: created
      listdata[2],                          # 1: state
      listdata[3],                          # 2: address
      listdata[4],                          # 3: session_id
      listdata[5],                          # 4: sequence number
      listdata[6]*1000000000+listdata[7],   # t1
      listdata[8]*1000000000+listdata[9],   # t2
      listdata[10]*1000000000+listdata[11], # t3
      listdata[12]*1000000000+listdata[13], # t4
      None,                                 # In order
      None,                                 # rtt
      None                                  # Delay variation
      )

    return Probe(clist) 

class Probe:
    """ A probe. """

    created = None
    t1 = None
    t2 = None
    t3 = None
    t4 = None
    rtt = None
    delay_variation = None
    in_order = None

    addr = None
    session_id = None
    seq = None

    has_given = None
    has_gotten = None

    def __init__(self, data):

        self.created = data[0]
        self.state = data[1]
        self.addr = data[2]
        self.session_id = data[3]
        self.seq = data[4]
        self.t1 = data[5]
        self.t2 = data[6]
        self.t3 = data[7]
        self.t4 = data[8]
        self.in_order = data[9]
        self.rtt = data[10]
        self.delay_variation = data[11]

        self.has_given = False
        self.has_gotten = False

        if self.rtt is None:
          self.rtt = self.getRtt()
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

    def getRtt(self):
        """ Calculates the rtt of the probe. """

        if self.successful():
            return (self.t4 - self.t1) - (self.t3 - self.t2)
        else:
            return None

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
            't1': self.t1,
            't2': self.t2,
            't3': self.t3,
            't4': self.t4,
            'rtt': self.rtt,
            'delayvar': self.delay_variation
        }

    def lost(self):
        """ Was the packet lost? """
        
        return self.state == STATE_TIMEOUT

    def successful(self):
        """ Do we have all timestamp? """

        return self.state == STATE_OK

    def set_prev_probe(self, prev_probe):
        """ Perform calculations which require previous probe.
        """
        try:
            self.delay_variation = self.rtt - prev_probe.rtt
        except TypeError:
            # catch error when RTT is None
            pass

        self.has_gotten = True
        prev_probe.has_given = True

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

    def avg_rtt(self):
        """ Find the average RTT 
        
        TODO: prevent overflow
        """
        t = time.time()
        sum = Timespec(0, 0)
        
        successful = 0
        for p in self:
            if p.successful():
                sum += p.rtt
                successful += 1

        print "avg_rtt: %f" % (time.time() - t)
        if successful < 1:
            return None
        return sum/successful


    def max_rtt(self):
        """ Find the max RTT """
        t = time.time()
        r = []
        for p in self:
            if p.successful():
              r.append(p.rtt)
        print "max_rtt: %f" % (time.time() - t)

        if len(r) < 1:
            return None
        return max(r)


    def min_rtt(self):
        """ Find the min RTT """
        t = time.time()
        r = []
        for p in self:
            if p.successful():
                r.append(p.rtt)
        print "min_rtt: %f" % (time.time() - t)
        if len(r) < 1:
            return None
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
            if p.successful():
                r.append(p.rtt)

        if len(r) < 1:
            return None

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

    def successful(self):
        """ Return number of successful probes """

        s = 0
        for p in self:
            if p.successful(): 
                s += 1

        return s
