#!/usr/bin/env python3
"""Checks that the mock printer (mock-printer.py) received a complete print.

    tools/haiku/test/check-mock-print.py [--state DIR] [--since EPOCH]

Reads DIR/events.jsonl and passes when, after --since:
- a client logged in to MQTT with the access code and asked for pushall;
- a .3mf was uploaded over FTPS, it is a ZIP archive with G-code in it
  (Metadata/plate_N.gcode), and the G-code's MD5 matches the archive's
  Metadata/plate_N.gcode.md5 when there is one;
- a print.project_file command arrived after the upload and names that file
  (url ftp://<file> or file:///sdcard/<file>, or the subtask name).
Prints what it found and exits 0 (pass) or 1.
"""
import argparse
import hashlib
import json
import pathlib
import sys
import zipfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--state', default='/data1/scratch/bambustudio/mock-printer')
    parser.add_argument('--since', type=float, default=0)
    args = parser.parse_args()
    events = []
    with open(pathlib.Path(args.state) / 'events.jsonl', encoding='utf-8') as f:
        for line in f:
            e = json.loads(line)
            if e['time'] >= args.since:
                events.append(e)
    ok = True

    def check(cond, text):
        nonlocal ok
        print(('PASS ' if cond else 'FAIL ') + text)
        ok = ok and cond

    logins = [e for e in events if e['event'] == 'mqtt_connect' and e.get('accepted')]
    check(bool(logins), f'MQTT login with the access code ({len(logins)} sessions)')
    pushall = [e for e in events if e['event'] == 'mqtt_request' and e.get('command') == 'pushall']
    check(bool(pushall), f'pushall requests: {len(pushall)}')
    stors = [e for e in events if e['event'] == 'ftp_stor' and e['path'].endswith('.3mf')]
    check(bool(stors), 'a .3mf uploaded over FTPS' + (f": {stors[-1]['path']} "
                                                       f"({stors[-1]['size']} bytes)" if stors else ''))
    if stors:
        stor = stors[-1]
        try:
            with zipfile.ZipFile(stor['file']) as z:
                names = z.namelist()
                gcodes = [n for n in names if n.startswith('Metadata/plate_') and n.endswith('.gcode')]
                check(bool(gcodes), f'G-code in the archive: {gcodes}')
                for g in gcodes:
                    data = z.read(g)
                    lines = data.count(b'\n')
                    md5 = hashlib.md5(data).hexdigest()
                    if g + '.md5' in names:
                        want = z.read(g + '.md5').decode().strip().lower()
                        check(md5 == want, f'{g}: {lines} lines, md5 {md5} matches {g}.md5')
                    else:
                        print(f'     {g}: {lines} lines, md5 {md5} (no .md5 in the archive)')
        except zipfile.BadZipFile as e:
            check(False, f'the upload is a ZIP archive ({e})')
        name = pathlib.PurePosixPath(stor['path']).name
        starts = [e for e in events if e['event'] == 'mqtt_request' and e.get('command') == 'project_file'
                  and e['time'] >= stor['time']]
        check(bool(starts), 'print.project_file after the upload')
        if starts:
            body = starts[-1].get('body') or {}
            url = body.get('url', '')
            check(name in url or name.startswith(body.get('subtask_name', '\0')),
                  f"project_file names the upload: url={url!r} param={body.get('param')!r} "
                  f"subtask_name={body.get('subtask_name')!r} use_ams={body.get('use_ams')}")
    print('RESULT: ' + ('PASS' if ok else 'FAIL'))
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
