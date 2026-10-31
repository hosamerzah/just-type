import sys
from pathlib import Path
import tempfile
import hashlib
import json
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
import build_identity
import bootstrap
import backup_bank
import sqlite3
from contextlib import closing

class BuildTools(unittest.TestCase):
    def test_online_backup_includes_live_wal_and_rejects_overwrite(self):
        with tempfile.TemporaryDirectory() as tmp:
            source=Path(tmp)/'words.db';destination=Path(tmp)/'backup.db'
            connection=sqlite3.connect(source)
            try:
                connection.execute('PRAGMA journal_mode=WAL')
                connection.execute('CREATE TABLE words(word TEXT)')
                connection.execute("INSERT INTO words VALUES('fixture')");connection.commit()
                digest=backup_bank.backup_bank(source,destination)
                with closing(sqlite3.connect(destination)) as restored:self.assertEqual(restored.execute('SELECT word FROM words').fetchall(),[('fixture',)])
                self.assertEqual(digest,hashlib.sha256(destination.read_bytes()).hexdigest())
                with self.assertRaises(FileExistsError):backup_bank.backup_bank(source,destination)
            finally:connection.close()

    def test_dependency_hash_rejects_corruption(self):
        blob=b'expected dependency'
        digest=hashlib.sha256(blob).hexdigest()
        bootstrap.verify_blob(blob,digest)
        with self.assertRaises(ValueError):bootstrap.verify_blob(blob+b'changed',digest)

    def test_stage_mismatch_and_changes_are_rejected(self):
        with tempfile.TemporaryDirectory() as tmp, patch.object(build_identity,'source_id',return_value='current'), patch.object(build_identity,'verify_embedded_identity'):
            folder=Path(tmp);(folder/'AutoKeyboardIME.dll').write_bytes(b'test fixture')
            data=dict(source='current',version='0.2.0',architecture='x64',files=build_identity.file_hashes(folder))
            manifest=folder/'stage-build.json';manifest.write_text(json.dumps(data))
            build_identity.validate_stage(folder,'x64')
            with self.assertRaises(ValueError):build_identity.validate_stage(folder,'x86')
            data['source']='old';manifest.write_text(json.dumps(data))
            with self.assertRaises(ValueError):build_identity.validate_stage(folder,'x64')
            data['source']='current';manifest.write_text(json.dumps(data))
            (folder/'AutoKeyboardIME.dll').write_bytes(b'changed')
            with self.assertRaises(ValueError):build_identity.validate_stage(folder,'x64')

    def test_old_binary_cannot_receive_new_source_label(self):
        with tempfile.TemporaryDirectory() as tmp:
            folder=Path(tmp)
            for name in ('AutoKeyboard.exe','AutoKeyboardIME.dll','AutoKeyboardControl.exe','AutoKeyboardPdfWorker.exe','AutoKeyboardTypingTest.exe','AutoKeyboardNotepadDiagnostic.exe'):
                (folder/name).write_bytes(b'old source identity')
            with self.assertRaises(ValueError):build_identity.verify_embedded_identity(folder,'new identity')

if __name__=='__main__':unittest.main()
