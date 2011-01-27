from struct import unpack
from timespec import Timespec

class Probe:
    """ A probe.

    """

    created = None
    t1 = None
    t2 = None
    t3 = None
    t4 = None

    self.addr = None
    self.msess_id = None
    self.seq = None

    def __init__(self, data):

        data = unpack('llc16siillllllll16s', data)

        self.created = Timespec(data[0], data[1])
        self.state = data[2]
        self.addr = data[3]
        self.msess_id = data[4]
        self.seq = data[5]
        self.t1 = Timespec(data[6], data[7])
        self.t2 = Timespec(data[8], data[9])
        self.t3 = Timespec(data[10], data[11])
        self.t4 = Timespec(data[12], data[13])

