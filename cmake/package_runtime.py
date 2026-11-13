"""Write a canonical two-file runtime ZIP accepted by CodecAddIn's strict reader.

Do not use native directory separators in ZIP member names. In particular,
Compress-Archive on Windows can preserve backslashes in the original headers.
"""
import argparse
import hashlib
from pathlib import Path
import re
import stat
import zipfile


def package(dll, output):
    if not re.fullmatch(r'ttp_(?:ofr|mkv|wv)\.dll', dll.name):
        raise ValueError('Unexpected plugin DLL name')
    data = dll.read_bytes()
    if len(data) < 64 or data[:2] != b'MZ':
        raise ValueError('Expected a built plugin PE image')
    name = 'AddIn/' + dll.name
    sums = (hashlib.sha256(data).hexdigest() + '  ' + name + '\n').encode('utf-8')
    output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(output, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for member, payload in ((name, data), ('SHA256SUMS.txt', sums)):
            entry = zipfile.ZipInfo(member, (1980, 1, 1, 0, 0, 0))
            entry.compress_type = zipfile.ZIP_DEFLATED
            entry.external_attr = (stat.S_IFREG | 0o644) << 16
            archive.writestr(entry, payload, compresslevel=9)
    return output


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--dll', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    print(package(args.dll, args.output))
