"""Execute the actual Apelang game rules using the hosted VM, without its frame loop."""
import pathlib
import subprocess
import sys
import tempfile
import unittest

EXE=str(pathlib.Path(sys.argv.pop(1)).resolve())
GAME=pathlib.Path("programs/snake.ape").read_text().split("# Main loop.")[0]

class SnakeRules(unittest.TestCase):
    def run_rules(self,source,expected):
        with tempfile.TemporaryDirectory() as temp:
            path=pathlib.Path(temp)/"rules.ape"
            path.write_text(GAME+'\n'+source)
            p=subprocess.run([EXE,'compile',str(path)],capture_output=True,text=True,timeout=10)
            self.assertEqual(p.returncode,0,p.stderr)
            p=subprocess.run([EXE,'run',str(path.with_suffix('.apb'))],capture_output=True,text=True,timeout=10)
            self.assertEqual(p.returncode,0,p.stderr)
            self.assertIn(expected,p.stdout)

    def test_eating_grows_and_scores(self):
        self.run_rules('reset() playing = true foodx = 16 foody = 7 step() tree score tree length tree xs[0] tree ys[0]', '1\n5\n16\n7\n')

    def test_wall_collision(self):
        self.run_rules('reset() playing = true xs[0] = 29 step() tree playing tree score', 'false\n0\n')

    def test_self_collision(self):
        self.run_rules('reset() playing = true dx = -1 step() tree playing', 'false\n')

    def test_reverse_is_ignored_and_turning_works(self):
        self.run_rules('reset() playing = true turn("left") tree dx turn("up") step() tree xs[0] tree ys[0]', '1\n15\n6\n')

    def test_can_move_into_vacated_tail(self):
        self.run_rules('reset() playing = true xs[0] = 1 ys[0] = 1 xs[1] = 1 ys[1] = 2 xs[2] = 2 ys[2] = 2 xs[3] = 2 ys[3] = 1 foodx = 20 foody = 10 step() tree playing tree xs[0] tree ys[0]', 'true\n2\n1\n')

    def test_food_does_not_spawn_inside_snake(self):
        self.run_rules('reset() ape valid = true swing 500 { place_food() ape i = 0 banana (i < length) { if (xs[i] == foodx ripe ys[i] == foody) { valid = false } i = i ooh 1 } } tree valid', 'true\n')

    def test_maximum_length_wins_without_array_overflow(self):
        self.run_rules('reset() playing = true length = 127 foodx = 16 foody = 7 step() tree won tree playing tree length', 'true\nfalse\n128\n')

if __name__=='__main__': unittest.main(verbosity=2)
