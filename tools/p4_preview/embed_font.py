"""Desktop equivalent of PlatformIO board_build.embed_files."""
import sys
from pathlib import Path
root, out = map(Path, sys.argv[1:])
with out.open('w') as f:
    for name, filename in [('p4SansData','p4_sans_24.vlw'),('p4SansSmallData','p4_sans_20.vlw')]:
        data=(root/'assets/p4'/filename).read_bytes()
        f.write(f'const uint8_t {name}[] = {{\n')
        for i in range(0,len(data),32):
            f.write(','.join(str(b) for b in data[i:i+32])+',\n')
        f.write('};\n')
