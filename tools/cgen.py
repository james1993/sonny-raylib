"""Writing C literals, for the generators that turn the tables into source.

    from cgen import c_float, c_string, c_double
"""


def c_float(value):
    """A C float literal that round-trips, whatever Python type it came in as."""
    return '%.6ff' % float(value)


def c_double(value):
    """A double that round-trips: a whole number as one, anything else in
    Python's shortest exact form."""
    v = float(value)
    if v == int(v) and abs(v) < 1e15:
        return '%d' % int(v)
    return repr(v)


def c_string(value):
    """A C string literal. Anything outside printable ASCII goes in as octal
    escapes of its UTF-8 bytes, three digits each so a digit after one can
    never be read as part of it."""
    if value is None:
        return '""'
    out = []
    for ch in str(value):
        if ch == '\\':
            out.append('\\\\')
        elif ch == '"':
            out.append('\\"')
        elif ch == '\n':
            out.append('\\n')
        elif ch == '\t':
            out.append('\\t')
        elif ord(ch) < 32 or ord(ch) > 126:
            out.extend('\\%03o' % b for b in ch.encode('utf-8'))
        else:
            out.append(ch)
    return '"%s"' % ''.join(out)
