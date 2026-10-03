"""Package standalone input without mutating the source tree or inference package."""
import argparse
import hashlib
import json
import tarfile
from pathlib import Path
from check_input_package import check


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[2] / 'upm/com.blazetc.humanvision.input')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    errors = check(args.root)
    if errors:
        raise ValueError('\n'.join(errors))
    args.output.mkdir(parents=True, exist_ok=True)
    version = json.loads((args.root / 'package.json').read_text())['version']
    package = args.output / ('com.blazetc.humanvision.input-' + version + '.tgz')
    if package.exists():
        raise FileExistsError('Use an immutable new output directory.')
    files = {p.relative_to(args.root).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
             for p in sorted(args.root.rglob('*')) if p.is_file()}
    with tarfile.open(package, 'w:gz', format=tarfile.PAX_FORMAT) as archive:
        for relative in files:
            archive.add(args.root / relative, arcname='package/' + relative)
    (args.output / 'input-source-sha256.json').write_text(json.dumps(files, indent=2) + '\n')
    (args.output / 'input-package-sha256.json').write_text(json.dumps({package.name: hashlib.sha256(package.read_bytes()).hexdigest()}, indent=2) + '\n')
    print(package)


if __name__ == '__main__':
    main()
