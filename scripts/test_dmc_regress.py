"""Disc-free checks for frame comparison: python scripts/test_dmc_regress.py."""
import unittest
import tempfile
from types import SimpleNamespace
from unittest.mock import patch

from PIL import Image
import dmc_regress
from dmc_regress import mean_p99


class FrameComparisonTests(unittest.TestCase):
    def test_identical(self):
        frame = Image.new('RGB', (10, 10), (20, 40, 60))
        self.assertEqual(mean_p99(frame, frame), (0, 0))

    def test_each_channel_counts_equally(self):
        black = Image.new('RGB', (10, 10))
        for color in ((30, 0, 0), (0, 30, 0), (0, 0, 30)):
            with self.subTest(color=color):
                self.assertEqual(mean_p99(black, Image.new('RGB', black.size, color)), (10, 30))

    def test_channel_error_cannot_hide_in_luminance(self):
        black = Image.new('RGB', (10, 10))
        blue = Image.new('RGB', black.size, (0, 0, 15))
        mean, p99 = mean_p99(black, blue)
        self.assertGreater(mean, 0.25)
        self.assertGreater(p99, 12)

    def test_size_mismatch(self):
        self.assertEqual(mean_p99(Image.new('RGB', (2, 2)), Image.new('RGB', (3, 2))), (255, 255))


class RunValidationTests(unittest.TestCase):
    def test_nonzero_exit_fails_even_with_frame_limit_message(self):
        case = dict(name='synthetic', frames=1, state=None, presses=[], checks=[], shots=[])
        result = SimpleNamespace(returncode=1, stderr='[ee] halted: frame limit reached\n')
        with tempfile.TemporaryDirectory() as out, patch.object(dmc_regress, 'WORK', out), \
                patch.object(dmc_regress.subprocess, 'run', return_value=result):
            problems, _, _ = dmc_regress.run_case(case, False)
        self.assertEqual(problems, ['runner exited with status 1'])

    def test_missing_state_fails(self):
        with patch('sys.argv', ['dmc_regress.py', '--only', 'mission1-idle']), \
                patch.object(dmc_regress.os.path, 'exists', side_effect=lambda path: not path.endswith('ground17000.ps2')), \
                patch('builtins.print'), self.assertRaises(SystemExit) as error:
            dmc_regress.main()
        self.assertEqual(error.exception.code, 1)

    def test_unknown_case_is_rejected(self):
        with patch('sys.argv', ['dmc_regress.py', '--only', 'typo']), \
                patch.object(dmc_regress.os.path, 'exists', return_value=True), \
                patch('sys.stderr'), self.assertRaises(SystemExit) as error:
            dmc_regress.main()
        self.assertEqual(error.exception.code, 2)

    def test_output_directory_option(self):
        with patch.object(dmc_regress, 'WORK', 'default-output'), \
                patch('sys.argv', ['dmc_regress.py', '--only', 'mission1-idle', '--out', 'isolated-output']), \
                patch.object(dmc_regress.os.path, 'exists', side_effect=lambda path: not path.endswith('ground17000.ps2')), \
                patch('builtins.print'):
            with self.assertRaises(SystemExit):
                dmc_regress.main()
            self.assertEqual(dmc_regress.WORK, dmc_regress.os.path.abspath('isolated-output'))


if __name__ == '__main__':
    unittest.main()
