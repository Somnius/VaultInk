#!/usr/bin/env python3
"""Writes data/columns.pdf and data/cid.pdf (run from test/pdf_text).
columns.pdf: page 1 three text columns drawn row by row, page 2 four columns, page 3 a table of short
cells (must stay in row order), page 4 two columns under a title (the old case, with line-long rows).
cid.pdf: page 1 a Type0 font with /Encoding /UniGB-UCS2-H and no ToUnicode (the codes are UTF-16), page 2 a
UTF-16 CMap with a surrogate pair, page 3 Identity-H without ToUnicode (nothing readable), page 4 plain
text plus a few Identity-H glyphs (the plain text stays). cid-only.pdf: just the Identity-H page."""
import sys

def build(objs, path):
    out = bytearray(b"%PDF-1.4\n")
    off = {}
    for n in sorted(objs):
        off[n] = len(out)
        body = objs[n] if isinstance(objs[n], bytes) else objs[n].encode("latin1")
        out += b"%d 0 obj\n" % n + body + b"\nendobj\n"
    xr = len(out)
    out += b"xref\n0 %d\n0000000000 65535 f \n" % (max(objs) + 1)
    for n in range(1, max(objs) + 1):
        out += (b"%010d 00000 n \n" % off[n]) if n in off else b"0000000000 65535 f \n"
    out += b"trailer\n<< /Size %d /Root 1 0 R >>\nstartxref\n%d\n%%%%EOF\n" % (max(objs) + 1, xr)
    open(path, "wb").write(out)

def stream(data):
    return b"<< /Length %d >>\nstream\n" % len(data) + data + b"\nendstream"

def pages_pdf(contents, fonts, path):
    """contents: list of page content bytes; fonts: dict resource name -> font object body."""
    objs = {1: "<< /Type /Catalog /Pages 2 0 R >>"}
    first = 10
    kids = []
    font_objs = {}
    res = " ".join("/%s %d 0 R" % (name, 3 + i) for i, name in enumerate(fonts))
    for i, name in enumerate(fonts):
        objs[3 + i] = fonts[name]
    for i, c in enumerate(contents):
        page, cont = first + 2 * i, first + 2 * i + 1
        kids.append("%d 0 R" % page)
        objs[page] = "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Contents %d 0 R /Resources << /Font << %s >> >> >>" % (cont, res)
        objs[cont] = stream(c)
    objs[2] = "<< /Type /Pages /Kids [%s] /Count %d >>" % (" ".join(kids), len(contents))
    build(objs, path)

helv = "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding /FirstChar 32 /LastChar 126 /Widths [" + " ".join(["500"] * 95) + "] >>"

def columns_page(xs, tag, tail):
    c = b""
    for row in range(14):
        for k, x in enumerate(xs):
            c += b"BT /F1 10 Tf %d %d Td (%s%d line %02d%s) Tj ET\n" % (x, 700 - row * 14, tag, k + 1, row + 1, tail)
    return c

p1 = columns_page([40, 230, 420], b"C", b" of the lorem ipsum text")
p2 = columns_page([20, 160, 300, 440], b"D", b" lorem ipsum")
# table: four short cells per row, drawn row by row
p3 = b""
for row in range(14):
    for k, x in enumerate([40, 190, 340, 490]):
        p3 += b"BT /F1 10 Tf %d %d Td (r%02dc%d) Tj ET\n" % (x, 700 - row * 14, row + 1, k + 1)
p4 = b"BT /F1 12 Tf 150 760 Td (A title across both columns) Tj ET\n"
for row in range(14):
    y = 700 - row * 14
    p4 += b"BT /F1 10 Tf 72 %d Td (Left %02d is a long line of ordinary prose) Tj ET\n" % (y, row + 1)
    p4 += b"BT /F1 10 Tf 320 %d Td (Right %02d is a long line of ordinary prose) Tj ET\n" % (y, row + 1)
pages_pdf([p1, p2, p3, p4], {"F1": helv}, "data/columns.pdf")

def type0(encoding):
    return ("<< /Type /Font /Subtype /Type0 /BaseFont /Arial /Encoding /%s /DescendantFonts [<< /Type /Font /Subtype /CIDFontType2 "
            "/BaseFont /Arial /CIDSystemInfo << /Registry (Adobe) /Ordering (Identity) /Supplement 0 >> /DW 1000 >>] >>" % encoding)

def hexs(text):
    data = text.encode("utf-16-be")
    return "<" + data.hex().upper() + ">"

def show(font, text_hex, y=700):
    return ("BT /%s 14 Tf 72 %d Td %s Tj ET\n" % (font, y, text_hex)).encode()

ucs2 = show("F1", hexs("Hello 世界 Ωμέγα"))
utf16 = show("F2", hexs("Pair \U0001F600 done"))
ident = b"".join(show("F3", "<00010002000300040005>", 700 - 20 * i) for i in range(6))
mixed = show("F3", "<000100020003>") + b"".join(
    b"BT /F4 12 Tf 72 %d Td (Plain text %d stays readable) Tj ET\n" % (650 - 20 * i, i) for i in range(6))
fonts = {"F1": type0("UniGB-UCS2-H"), "F2": type0("UniJIS-UTF16-H"), "F3": type0("Identity-H"), "F4": helv}
pages_pdf([ucs2, utf16, ident, mixed], fonts, "data/cid.pdf")
pages_pdf([ident], fonts, "data/cid-only.pdf")
