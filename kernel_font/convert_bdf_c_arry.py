# convert_bdf_to_c_array.py
# Converts a Terminus BDF (8x16) to a C header for kernel use

bdf_file = "./terminus-font-4.49.1/ter-u16b.bdf"
header_file = "font.h"

font = [[0]*16 for _ in range(256)]
current_char = None
bitmap_row = 0

with open(bdf_file, "r") as f:
    for line in f:
        line = line.strip()
        if line.startswith("STARTCHAR"):
            current_char = None
            bitmap_row = 0
        elif line.startswith("ENCODING"):
            code = int(line.split()[1])
            if 0 <= code <= 255:
                current_char = code
        elif line.startswith("BITMAP") and current_char is not None:
            bitmap_row = 0
        elif line.startswith("ENDCHAR"):
            current_char = None
        elif current_char is not None and all(c in "0123456789ABCDEF" for c in line):
            font[current_char][bitmap_row] = int(line, 16)
            bitmap_row += 1

with open(header_file, "w") as f:
    f.write("// Terminus 16px normal font, ASCII 0-255\n")
    f.write("inline unsigned char font[256][16] = {\n")
    for i in range(256):
        f.write("  {")
        f.write(",".join(f"0x{b:02X}" for b in font[i]))
        f.write("},\n")
    f.write("};\n")

print(f"font.h generated from {bdf_file}")
