#!/usr/bin/env python3
"""Writes data/hand-made.pdf: two pages that exercise what the generated files don't.
Page 1: content stream filtered by ASCII85 then Flate, a TJ array with kerning and word-space kerns,
a Differences encoding, and text inside a Form XObject.
Page 2: every word drawn by its own `q ... cm BT ... Td Tj` block with a font size of 1, as Quartz does,
and a gap between the words that only the positions show; plus two filled-in form fields (one
UTF-16, one Latin-1) that go after the text."""
import base64, zlib, sys

objs = {}

def add(n, body):
    objs[n] = body if isinstance(body, bytes) else body.encode('latin1')

def stream(d, data):
    return f"<< {d} /Length {len(data)} >>\nstream\n".encode('latin1') + data + b"\nendstream"

page1 = (b"BT /F1 12 Tf 72 700 Td [(Hello) -300 (world) -300 (from) -300 (ASCII85)] TJ ET\n"
         b"BT /F1 12 Tf 72 680 Td (second line has a caf\\351 and ) Tj /F2 12 Tf (\\001\\002) Tj ET\n"
         b"BT /F1 12 Tf 72 640 Td (a fresh paragraph far below) Tj ET\n"
         b"/Fm1 Do\n")
a85 = base64.a85encode(zlib.compress(page1), adobe=True)
form = b"BT /F1 12 Tf 72 600 Td (inside a form) Tj ET"
page2 = b""
word_x = 100.0
for word in ("Quartz", "draws", "each", "word"):
    page2 += b"q 12 0 0 12 %.2f 700 cm BT 1 0 0 1 0 0 Tm /F1 1 Tf (%s) Tj ET Q\n" % (word_x, word.encode())
    word_x += 12 * (len(word) * 0.5 + 0.5)  # glyph widths are 500 each; half an em of space follows

add(1, "<< /Type /Catalog /Pages 2 0 R >>")
add(2, "<< /Type /Pages /Kids [3 0 R 4 0 R] /Count 2 >>")
add(3, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Contents 5 0 R /Resources << /Font << /F1 7 0 R /F2 8 0 R >> /XObject << /Fm1 9 0 R >> >> >>")
add(4, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Contents 6 0 R /Annots [10 0 R 11 0 R] /Resources << /Font << /F1 7 0 R >> >> >>")
add(5, stream("/Filter [/ASCII85Decode /FlateDecode]", a85))
add(6, stream("/Filter /FlateDecode", zlib.compress(page2)))
add(7, "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding /FirstChar 32 /LastChar 126 /Widths [" + " ".join(["500"] * 95) + "] >>")
add(8, "<< /Type /Font /Subtype /Type1 /BaseFont /Symbol /Encoding << /Type /Encoding /Differences [1 /eacute /uni03B1] >> >>")
add(10, "<< /Type /Annot /Subtype /Widget /FT /Tx /T (Name) /V (Ceren Yurdakul) /Rect [0 0 1 1] >>")
add(11, "<< /Type /Annot /Subtype /Widget /FT /Tx /T (Clan) /V <FEFF0391039B03A6> /Rect [0 0 1 1] >>")
add(9, stream("/Type /XObject /Subtype /Form /BBox [0 0 612 792] /Resources << /Font << /F1 7 0 R >> >>", form))

out = bytearray(b"%PDF-1.4\n")
offsets = {}
for n in sorted(objs):
    offsets[n] = len(out)
    out += f"{n} 0 obj\n".encode() + objs[n] + b"\nendobj\n"
xref = len(out)
out += f"xref\n0 {max(objs) + 1}\n0000000000 65535 f \n".encode()
for n in range(1, max(objs) + 1):
    out += f"{offsets[n]:010d} 00000 n \n".encode()
out += f"trailer\n<< /Size {max(objs) + 1} /Root 1 0 R >>\nstartxref\n{xref}\n%%EOF\n".encode()
open(sys.argv[1] if len(sys.argv) > 1 else "data/hand-made.pdf", "wb").write(out)
