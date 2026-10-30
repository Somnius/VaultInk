#!/usr/bin/env python3
"""Writes data/aes128.pdf: a one-page PDF encrypted with the standard security handler, revision 4,
AES-128 (crypt filter AESV2), empty user password. The page text and a form field's value are encrypted,
so the reader has to derive the key, check it against /U, and decrypt a stream and a string.
Needs python3 and the openssl command (for AES-CBC); RC4 and MD5 are done here."""
import hashlib, os, subprocess, sys

PAD = bytes.fromhex("28BF4E5E4E758A4164004E56FFFA01082E2E00B6D0683E802F0CA9FE6453697A")

def rc4(key, data):
    s = list(range(256)); j = 0
    for i in range(256):
        j = (j + s[i] + key[i % len(key)]) % 256; s[i], s[j] = s[j], s[i]
    out = bytearray(); i = j = 0
    for b in data:
        i = (i + 1) % 256; j = (j + s[i]) % 256; s[i], s[j] = s[j], s[i]
        out.append(b ^ s[(s[i] + s[j]) % 256])
    return bytes(out)

def aes_cbc(key, iv, data):
    return subprocess.run(["openssl", "enc", "-aes-128-cbc", "-K", key.hex(), "-iv", iv.hex()],
                          input=data, capture_output=True, check=True).stdout

file_id = bytes.fromhex("00112233445566778899aabbccddeeff")
owner = bytes(range(32))
perms = -4
key_len = 16
material = PAD + owner + (perms & 0xFFFFFFFF).to_bytes(4, "little") + file_id
h = hashlib.md5(material).digest()
for _ in range(50):
    h = hashlib.md5(h[:key_len]).digest()
file_key = h[:key_len]
# Algorithm 5: /U for revisions 3 and 4
u = rc4(file_key, hashlib.md5(PAD + file_id).digest())
for i in range(1, 20):
    u = rc4(bytes(b ^ i for b in file_key), u)
u += bytes(16)

def object_key(num, gen=0):
    return hashlib.md5(file_key + num.to_bytes(3, "little") + gen.to_bytes(2, "little") + b"sAlT").digest()[:16]

def encrypt(num, data):
    iv = hashlib.md5(b"iv%d" % num).digest()
    return iv + aes_cbc(object_key(num), iv, data)

def hexstr(b): return "<" + b.hex() + ">"

content = b"BT /F1 12 Tf 72 700 Td (Encrypted with AES-128 and read anyway) Tj ET"
objs = {}
objs[1] = b"<< /Type /Catalog /Pages 2 0 R >>"
objs[2] = b"<< /Type /Pages /Kids [3 0 R] /Count 1 >>"
objs[3] = b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Contents 4 0 R /Annots [6 0 R] /Resources << /Font << /F1 5 0 R >> >> >>"
enc = encrypt(4, content)
objs[4] = b"<< /Length %d >>\nstream\n" % len(enc) + enc + b"\nendstream"
objs[5] = b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>"
objs[6] = ("<< /Type /Annot /Subtype /Widget /FT /Tx /T " + hexstr(encrypt(6, b"Clan")) + " /V " +
           hexstr(encrypt(6, b"Ventrue")) + " /Rect [0 0 1 1] >>").encode()
objs[7] = (b"<< /Filter /Standard /V 4 /R 4 /Length 128 /P -4 /O " + hexstr(owner).encode() + b" /U " + hexstr(u).encode() +
           b" /CF << /StdCF << /CFM /AESV2 /AuthEvent /DocOpen /Length 16 >> >> /StmF /StdCF /StrF /StdCF >>")

out = bytearray(b"%PDF-1.6\n")
offsets = {}
for n in sorted(objs):
    offsets[n] = len(out)
    out += b"%d 0 obj\n" % n + objs[n] + b"\nendobj\n"
xref = len(out)
out += b"xref\n0 %d\n0000000000 65535 f \n" % (max(objs) + 1)
for n in range(1, max(objs) + 1):
    out += b"%010d 00000 n \n" % offsets[n]
out += (b"trailer\n<< /Size %d /Root 1 0 R /Encrypt 7 0 R /ID [" % (max(objs) + 1) + hexstr(file_id).encode() + b" " +
        hexstr(file_id).encode() + b"] >>\nstartxref\n%d\n%%%%EOF\n" % xref)
open(sys.argv[1] if len(sys.argv) > 1 else "data/aes128.pdf", "wb").write(out)
