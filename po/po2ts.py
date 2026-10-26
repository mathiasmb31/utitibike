#!/usr/bin/env python3
"""Convertit un catalogue gettext .po en fichier Qt Linguist .ts.

msgfmt --qt produit un .qm au vieux format qui n'accepte pas l'Unicode
dans les msgid (les chaînes source françaises cassent la compilation).
lrelease, lui, accepte l'Unicode partout : on passe donc par un .ts.

Usage : po2ts.py entree.po sortie.ts
"""
import sys
import xml.sax.saxutils as sax


def unescape(s):
    out = []
    i = 0
    while i < len(s):
        c = s[i]
        if c == "\\" and i + 1 < len(s):
            n = s[i + 1]
            if n == "n":
                out.append("\n")
            elif n == "t":
                out.append("\t")
            elif n == "r":
                out.append("\r")
            elif n == "\\":
                out.append("\\")
            elif n == '"':
                out.append('"')
            else:
                out.append(n)
            i += 2
        else:
            out.append(c)
            i += 1
    return "".join(out)


def po_unquote(s):
    s = s.strip()
    if len(s) >= 2 and s[0] == '"' and s[-1] == '"':
        return unescape(s[1:-1])
    return unescape(s)


def parse_po(path):
    entries = []
    ctxt = None
    mid = None
    mstr = None
    section = None
    header_lang = ""

    def flush():
        nonlocal ctxt, mid, mstr, header_lang
        if mid is not None:
            entries.append((ctxt, mid, mstr if mstr is not None else ""))
            if mid == "" and mstr:
                for line in mstr.split("\n"):
                    if line.startswith("Language:"):
                        header_lang = line.split(":", 1)[1].strip()
        ctxt = None
        mid = None
        mstr = None

    with open(path, encoding="utf-8") as f:
        for raw in f:
            line = raw.strip()
            if not line or line.startswith("#"):
                continue
            if line.startswith("msgctxt "):
                flush()
                section = "ctxt"
                ctxt = po_unquote(line[len("msgctxt "):])
            elif line.startswith("msgid "):
                if mid is not None:
                    flush()
                section = "id"
                mid = po_unquote(line[len("msgid "):])
            elif line.startswith("msgstr "):
                section = "str"
                mstr = po_unquote(line[len("msgstr "):])
            elif line.startswith('"'):
                val = po_unquote(line)
                if section == "ctxt" and ctxt is not None:
                    ctxt += val
                elif section == "id" and mid is not None:
                    mid += val
                elif section == "str" and mstr is not None:
                    mstr += val
    flush()
    return entries, header_lang


def to_ts(entries, lang, out_path):
    contexts = {}
    order = []
    for ctxt, mid, mstr in entries:
        if mid == "":
            continue
        name = ctxt if ctxt is not None else ""
        if name not in contexts:
            contexts[name] = []
            order.append(name)
        contexts[name].append((mid, mstr))

    lines = [
        '<?xml version="1.0" encoding="utf-8"?>',
        "<!DOCTYPE TS>",
        '<TS version="2.1" language="%s">' % sax.escape(lang or ""),
    ]
    for name in order:
        lines.append("<context>")
        lines.append("    <name>%s</name>" % sax.escape(name))
        for mid, mstr in contexts[name]:
            lines.append("    <message>")
            lines.append("        <source>%s</source>" % sax.escape(mid))
            lines.append("        <translation>%s</translation>" % sax.escape(mstr))
            lines.append("    </message>")
        lines.append("</context>")
    lines.append("</TS>")
    with open(out_path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")


def main():
    if len(sys.argv) != 3:
        sys.exit("usage: po2ts.py entree.po sortie.ts")
    entries, lang = parse_po(sys.argv[1])
    to_ts(entries, lang, sys.argv[2])


if __name__ == "__main__":
    main()
