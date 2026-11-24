"""Read and write WDBC files (the 3.3.5a client's DBFilesClient/*.dbc).

A WDBC file is a 20-byte header (magic, record count, field count, record size, string block size), the records
(here always 4-byte fields), then a string block of NUL-terminated UTF-8 strings that string fields point into by
byte offset. Offset 0 is the empty string.

Rows are kept as lists of raw uint32 values, so a file read and written again is byte-identical. New strings are
appended to the string block (an existing identical string is reused).
"""
import struct

MAGIC = b'WDBC'


def f32(u):
    """Raw uint32 -> float."""
    return struct.unpack('<f', struct.pack('<I', u))[0]


def u32f(f):
    """Float -> raw uint32."""
    return struct.unpack('<I', struct.pack('<f', f))[0]


def i32(u):
    """Raw uint32 -> signed int."""
    return u - (1 << 32) if u & 0x80000000 else u


class Dbc:
    def __init__(self, fields, rows=None, strings=b'\0'):
        self.fields = fields
        self.rows = rows if rows is not None else []
        self.strings = bytearray(strings)

    @classmethod
    def parse(cls, data):
        magic, count, fields, size, ssize = struct.unpack_from('<4s4I', data)
        if magic != MAGIC:
            raise ValueError('not a WDBC file')
        if size != fields * 4:
            raise ValueError('only 4-byte fields are supported (record size %d, %d fields)' % (size, fields))
        if len(data) != 20 + count * size + ssize:
            raise ValueError('size mismatch: %d bytes, header says %d' % (len(data), 20 + count * size + ssize))
        rows = [list(struct.unpack_from('<%dI' % fields, data, 20 + i * size)) for i in range(count)]
        return cls(fields, rows, data[20 + count * size:])

    @classmethod
    def load(cls, path):
        with open(path, 'rb') as f:
            return cls.parse(f.read())

    def string(self, offset):
        end = self.strings.index(b'\0', offset)
        return self.strings[offset:end].decode('utf8')

    def add_string(self, text):
        """Offset of `text` in the string block, appending it when it isn't there yet."""
        if not text:
            return 0
        raw = text.encode('utf8') + b'\0'
        # Reuse a whole string only (match at a string start), as the client's own files do.
        at = 0
        while True:
            at = self.strings.find(raw, at)
            if at < 0:
                break
            if at == 0 or self.strings[at - 1] == 0:
                return at
            at += 1
        at = len(self.strings)
        self.strings += raw
        return at

    def row(self, row_id):
        for r in self.rows:
            if r[0] == row_id:
                return r
        return None

    def ids(self):
        return {r[0] for r in self.rows}

    def serialize(self):
        out = bytearray(struct.pack('<4s4I', MAGIC, len(self.rows), self.fields, self.fields * 4, len(self.strings)))
        for r in self.rows:
            if len(r) != self.fields:
                raise ValueError('row %d has %d fields, expected %d' % (r[0], len(r), self.fields))
            out += struct.pack('<%dI' % self.fields, *[v & 0xFFFFFFFF for v in r])
        out += self.strings
        return bytes(out)

    def save(self, path):
        with open(path, 'wb') as f:
            f.write(self.serialize())
