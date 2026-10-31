"""Developer test helper: verified SQLite online backup before a live upgrade."""
from pathlib import Path
import argparse
import datetime
import hashlib
import os
import sqlite3
import time
import uuid
from contextlib import closing

def backup_bank(source, destination):
    source=Path(source).resolve();destination=Path(destination).resolve()
    if not source.is_file():raise FileNotFoundError('The personal word bank does not exist at the supplied path')
    if source==destination:raise ValueError('Backup must use a new file')
    destination.parent.mkdir(parents=True,exist_ok=True)
    descriptor=os.open(destination,os.O_CREAT|os.O_EXCL|os.O_WRONLY,0o600)
    os.close(descriptor)
    try:
        started=time.monotonic()
        def progress(status, remaining, total):
            if time.monotonic()-started>30:raise TimeoutError('Backup timed out; close the importer and try again')
        with closing(sqlite3.connect(source.as_uri()+'?mode=ro',uri=True,timeout=5)) as reader:
            writer=sqlite3.connect(destination)
            try:
                reader.backup(writer,pages=256,progress=progress,sleep=0.01)
                if writer.execute('PRAGMA quick_check').fetchall()!=[('ok',)]:raise ValueError('Backup failed its SQLite integrity check')
            finally:writer.close()
        digest=hashlib.sha256(destination.read_bytes()).hexdigest()
        destination.with_suffix(destination.suffix+'.sha256').write_text(f'{digest}  {destination.name}\n',encoding='utf-8')
        return digest
    except BaseException:
        # Only files belonging to the destination exclusively created above.
        for path in (destination,Path(str(destination)+'-wal'),Path(str(destination)+'-shm')):
            path.unlink(missing_ok=True)
        raise

if __name__=='__main__':
    parser=argparse.ArgumentParser()
    parser.add_argument('--bank',type=Path,default=Path(os.environ.get('LOCALAPPDATA',''))/'AutoKeyboard/words.db')
    parser.add_argument('--output',type=Path)
    args=parser.parse_args()
    name='words-'+datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ')+'-'+uuid.uuid4().hex+'.db'
    destination=args.output or args.bank.parent/'backups'/name
    try:
        digest=backup_bank(args.bank,destination)
        print(f'Verified backup: {destination.resolve()}\nSHA256: {digest}')
    except Exception as error:raise SystemExit(str(error))
