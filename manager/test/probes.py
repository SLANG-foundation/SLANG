import sqlite3
import time
from collections import defaultdict

class probes:
    conn = None
    curs = None
    db = defaultdict(lambda: defaultdict(dict))
    buff = []
    last = int(time.time())

    def __init__(self):
        self.conn = sqlite3.connect("/tmp/cp.sql")
        self.curs = self.conn.cursor()
        sql =  'CREATE TABLE IF NOT EXISTS probes '
        sql += '(type, id, seq, sec, nsec, UNIQUE (type, id, seq));'
        self.curs.execute(sql)
        self.conn.commit()

    def insert(self, d):
        self.buff.append(d)
        print('got ' + str(d[5]) + ' from ' + str(d[4])) 
        #if int(time.time()) > self.last:
        #    self.store()
        #    print('clear')

    def store(self):
        for d in self.buff:
            tmp = 'INSERT OR REPLACE INTO probes VALUES (?, ?, ?, ?, ?);'
            #self.curs.execute(tmp, d) 
        #self.conn.commit()
        self.buff = []
        self.last = int(time.time())
