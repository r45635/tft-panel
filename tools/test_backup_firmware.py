#!/usr/bin/env python3
"""Host test of tools/backup-firmware.py, esptool replaced by an in-memory board:
python3 -I tools/test_backup_firmware.py"""
import contextlib, importlib.util, io, os, random, tempfile, unittest
from unittest import mock

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("backup", os.path.join(HERE, "backup-firmware.py"))
backup = importlib.util.module_from_spec(spec)
spec.loader.exec_module(backup)
PIECE = backup.BOARDS[("ESP32-P4", 16)].piece


class FakeBoard:
    """`stops[offset]` = how many reads at that offset come back short yet exit 0: the size, not
    the exit code, is what proves a piece arrived (a real stop exits 2 and leaves no file)."""

    def __init__(self, flash, stops=None):
        self.flash, self.stops, self.reads = flash, dict(stops or {}), []

    def __call__(self, args, port, baud=None, capture=True, after=None):
        off, length, path = int(args[1], 16), int(args[2]), args[3]
        self.reads.append((off, length, after))
        cut = self.stops.get(off, 0) > 0
        self.stops[off] = self.stops.get(off, 0) - 1
        with open(path, "wb") as f:
            f.write(self.flash[off:off + (1000 if cut else length)])
        return 0, ""


class Backup(unittest.TestCase):
    def read(self, board, size):
        with tempfile.TemporaryDirectory() as d, contextlib.redirect_stdout(io.StringIO()), \
                mock.patch.object(backup, "esptool", board):
            path = os.path.join(d, "dump.bin")
            ok = backup.read_flash("/dev/x", 230400, size, PIECE, path)
            return ok, open(path, "rb").read() if ok else None, os.listdir(d)

    def test_pieces_cover_the_flash_in_order_with_a_short_last_one(self):
        flash = random.Random(1).randbytes(3 * PIECE + 100)
        board = FakeBoard(flash)
        self.assertEqual(self.read(board, len(flash)), (True, flash, ["dump.bin"]))
        self.assertEqual(board.reads, [(0, PIECE, "no-reset"), (PIECE, PIECE, "no-reset"),
                                       (2 * PIECE, PIECE, "no-reset"), (3 * PIECE, 100, "no-reset")])

    def test_a_dropped_piece_is_read_again_then_given_up_after_three(self):
        flash = random.Random(2).randbytes(2 * PIECE)
        self.assertEqual(self.read(FakeBoard(flash, {PIECE: 2}), len(flash))[:2], (True, flash))
        self.assertEqual(self.read(FakeBoard(flash, {PIECE: 3}), len(flash)), (False, None, []))


class Restore(unittest.TestCase):
    def test_each_board_is_restored_at_its_own_flash_speed(self):
        for env, baud in (("crowpanel7", 460800), ("waveshare-p4-7b", 921600),
                          ("p4-s070-wba", 230400)):
            out = io.StringIO()
            with contextlib.redirect_stdout(out):
                backup.print_restore("/dev/x", "/b/%s-factory-80f1b2-2026-11-15.bin" % env, None)
            self.assertIn("--baud %d write-flash" % baud, out.getvalue(), env)


if __name__ == "__main__":
    unittest.main()
