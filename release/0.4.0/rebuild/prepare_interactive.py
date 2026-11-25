"""Ask for this user's SD identity, then prepare the unified rebuild."""
import json
from pathlib import Path
import re
from coexist_rebuild import prepare


def main():
    drive = input('SD drive (for example H:): ').strip()
    serial = input('exFAT volume serial (for example ABCD-1234): ').strip()
    if not re.fullmatch(r'[D-Z]:[/\\]?', drive, re.I) or not re.fullmatch(r'[0-9a-fA-F]{4}-?[0-9a-fA-F]{4}', serial):
        raise SystemExit('Invalid drive or volume serial. No files were changed.')
    result = prepare(Path(__file__).resolve().parent, Path(drive[:2] + '/'), int(serial.replace('-', ''), 16))
    print(json.dumps(result, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()
