import tempfile
import unittest
import zipfile
from pathlib import Path
from unittest.mock import patch

import make_source_zip as source


class SourceArchiveTests(unittest.TestCase):
    def test_private_files_and_submodule_metadata_are_excluded(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            for name in ('src/core.c', 'local-review/credentials.txt', 'backups/board.img',
                         'ports/board.conf', 'third_party/tdsh/.git', '.env',
                         'build-debug/config.txt', 'board.example.conf'):
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text('test fixture')
            with zipfile.ZipFile(root / 'out.zip', 'w') as archive:
                source.add_tree(archive, root, 'source', [0, 0])
            with zipfile.ZipFile(root / 'out.zip') as archive:
                self.assertEqual(set(archive.namelist()), {'source/src/core.c', 'source/board.example.conf'})

    def test_neighboring_private_site_is_not_automatically_included(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp) / 'tinydesk'
            root.mkdir()
            (root / 'README.md').write_text('public')
            site = Path(tmp) / 'tinydesk-site'
            site.mkdir()
            (site / 'private.md').write_text('private')
            with patch.object(source, 'ROOT', str(root)), patch.object(source, 'version', return_value='test'), patch('sys.argv', ['make_source_zip.py', '--out', tmp]):
                source.main()
            with zipfile.ZipFile(Path(tmp) / 'TinyDesk-test-source.zip') as archive:
                self.assertFalse(any('private.md' in name for name in archive.namelist()))


if __name__ == '__main__':
    unittest.main()
