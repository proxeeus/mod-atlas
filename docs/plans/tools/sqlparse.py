"""Tiny mysqldump parser: load(path) -> (columns, rows as dicts). Read-only, file-based."""
import re


def _columns(text, table):
    m = re.search(r"CREATE TABLE `%s` \((.*?)\n\)" % re.escape(table), text, re.S)
    if not m:
        return None
    cols = []
    for line in m.group(1).splitlines():
        line = line.strip()
        cm = re.match(r"`([^`]+)`", line)
        if cm:
            cols.append(cm.group(1))
    return cols


def _tuples(s, i):
    n = len(s)
    while i < n:
        c = s[i]
        if c == '(':
            i += 1
            row, cur, inq, quoted = [], [], False, False
            while True:
                c = s[i]
                if inq:
                    if c == '\\':
                        nxt = s[i + 1]
                        cur.append({'n': '\n', 'r': '\r', 't': '\t', '0': '\0'}.get(nxt, nxt))
                        i += 2
                        continue
                    if c == "'":
                        inq = False
                    else:
                        cur.append(c)
                    i += 1
                    continue
                if c == "'":
                    inq, quoted = True, True
                elif c == ',' or c == ')':
                    v = ''.join(cur).strip()
                    if not quoted:
                        if v == 'NULL':
                            v = None
                        else:
                            try:
                                v = int(v)
                            except ValueError:
                                try:
                                    v = float(v)
                                except ValueError:
                                    pass
                    row.append(v)
                    cur, quoted = [], False
                    if c == ')':
                        i += 1
                        yield row
                        break
                else:
                    cur.append(c)
                i += 1
        elif c == ';':
            return
        else:
            i += 1


def load(path, table=None):
    with open(path, encoding='utf-8', errors='replace') as f:
        text = f.read()
    if table is None:
        table = re.search(r"CREATE TABLE `([^`]+)`", text).group(1)
    cols = _columns(text, table)
    rows = []
    for m in re.finditer(r"INSERT INTO `%s` VALUES" % re.escape(table), text):
        for t in _tuples(text, m.end()):
            rows.append(dict(zip(cols, t)))
    return cols, rows
