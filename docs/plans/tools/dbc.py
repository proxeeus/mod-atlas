import struct, sys
def load(path):
    b = open(path,'rb').read()
    magic, n, f, rs, ss = struct.unpack('<4s4I', b[:20])
    recs = b[20:20+n*rs]; strs = b[20+n*rs:]
    rows = [struct.unpack('<%dI' % (rs//4), recs[i*rs:(i+1)*rs]) for i in range(n)]
    def s(off):
        e = strs.index(b'\0', off); return strs[off:e].decode('utf8','replace')
    return rows, s
def f32(u): return struct.unpack('<f', struct.pack('<I', u))[0]
