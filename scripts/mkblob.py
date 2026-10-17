#!/usr/bin/env python3
import sys

src, dst, symbol = sys.argv[1], sys.argv[2], sys.argv[3]
data = open(src, "rb").read()
with open(dst, "w", encoding="utf-8") as out:
    out.write("#include <sobi/types.h>\n")
    out.write("const unsigned char %s[] = {\n" % symbol)
    for i, byte in enumerate(data):
        if i % 16 == 0:
            out.write("    ")
        out.write("%d," % byte)
        if i % 16 == 15 or i + 1 == len(data):
            out.write("\n")
    out.write("};\n")
    out.write("const unsigned int %s_len = %d;\n" % (symbol, len(data)))
