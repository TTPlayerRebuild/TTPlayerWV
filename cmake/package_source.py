"""Create an allowlisted, self-contained codec source attachment (no test data)."""
import argparse
import hashlib
from pathlib import Path
import re
import urllib.request
import zipfile

ARCHIVE = 'OptimFROG_Win_x86_SSE2_5100.zip'
OFR_HASH = '8d2c910cd7c1b730a16ed9357cd23708efa4312021c22814172b67af41f14b34'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--version', required=True)
    a = parser.parse_args(); root = a.root.resolve(); build = a.build.resolve()
    if not re.fullmatch(r'[0-9]{4}\.[0-9]{2}\.[0-9]{2}(?:p[1-9][0-9]*)?', a.version):
        raise ValueError('Invalid build version')
    plugin = 'ttp_wv' if 'project(TtpWv ' in (root / 'CMakeLists.txt').read_text() else 'ttp_ofr'
    destination = build / 'Release' / f'{plugin}-{a.version}-source.zip'
    with zipfile.ZipFile(destination, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        def tree(source, relative):
            for p in sorted(source.rglob('*')) if source.is_dir() else [source]:
                if p.is_symlink() or not p.is_file() or any(x in ('.git', '__pycache__') for x in p.parts):
                    continue
                if p.suffix.lower() in ('.obj', '.pdb', '.lib', '.exe', '.dll', '.pyc', '.log', '.o', '.a'):
                    continue
                z.write(p, plugin + '/' + (relative / p.relative_to(source) if source.is_dir() else relative).as_posix())
        for name in ('CMakeLists.txt', 'build.ps1', 'README.md', 'LICENSE', '.gitignore', '.clang-format', 'src', 'include', 'cmake', 'docs', '.github'):
            p = root / name
            if p.exists(): tree(p, Path(name))
        z.writestr(plugin + '/BUILD_VERSION', a.version + '\n')
        notice_folder = 'wavpack' if plugin == 'ttp_wv' else 'optimfrog'
        for notice in (root / 'third_party' / notice_folder).glob('*'):
            if notice.is_file() and notice.suffix.lower() != '.zip':
                tree(notice, Path('third_party') / notice_folder / notice.name)
        if plugin == 'ttp_wv':
            tree(Path((build / 'codec-source-path.txt').read_text()), Path('third_party/wavpack/source'))
        else:
            # Preserve the complete upstream release package and its original
            # name, rather than redistribute a modified SDK/encoder subset.
            archive = root / 'third_party' / ARCHIVE
            if not archive.exists():
                archive = build / ARCHIVE
                if not archive.exists():
                    urllib.request.urlretrieve('http://losslessaudio.org/Downloads/A1C0/5100/Win/' + ARCHIVE, archive)
            if hashlib.sha256(archive.read_bytes()).hexdigest() != OFR_HASH:
                raise ValueError('OptimFROG release archive SHA256 mismatch')
            z.write(archive, plugin + '/third_party/' + ARCHIVE)
    print(destination)


if __name__ == '__main__': main()
