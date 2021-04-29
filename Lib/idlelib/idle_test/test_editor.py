"Test editor, coverage 35%."

from idlelib import editor
import functools
import unittest
from collections import namedtuple
from test.support import requires
from tkinter import Tk
from idlelib.idle_test.mock_idle import Func
from idlelib.idle_test.tkinter_testing_utils import run_in_tk_mainloop

Editor = editor.EditorWindow


class EditorWindowTest(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        requires('gui')
        cls.root = Tk()
        cls.root.withdraw()

    @classmethod
    def tearDownClass(cls):
        cls.root.update_idletasks()
        for id in cls.root.tk.call('after', 'info'):
            cls.root.after_cancel(id)
        cls.root.destroy()
        del cls.root

    def test_init(self):
        e = Editor(root=self.root)
        self.assertEqual(e.root, self.root)
        e._close()


class TestGetLineIndent(unittest.TestCase):
    def test_empty_lines(self):
        for tabwidth in [1, 2, 4, 6, 8]:
            for line in ['', '\n']:
                with self.subTest(line=line, tabwidth=tabwidth):
                    self.assertEqual(
                        editor.get_line_indent(line, tabwidth=tabwidth),
                        (0, 0),
                    )

    def test_tabwidth_4(self):
        #        (line, (raw, effective))
        tests = (('no spaces', (0, 0)),
                 # Internal space isn't counted.
                 ('    space test', (4, 4)),
                 ('\ttab test', (1, 4)),
                 ('\t\tdouble tabs test', (2, 8)),
                 # Different results when mixing tabs and spaces.
                 ('    \tmixed test', (5, 8)),
                 ('  \t  mixed test', (5, 6)),
                 ('\t    mixed test', (5, 8)),
                 # Spaces not divisible by tabwidth.
                 ('  \tmixed test', (3, 4)),
                 (' \t mixed test', (3, 5)),
                 ('\t  mixed test', (3, 6)),
                 # Only checks spaces and tabs.
                 ('\nnewline test', (0, 0)))

        for line, expected in tests:
            with self.subTest(line=line):
                self.assertEqual(
                    editor.get_line_indent(line, tabwidth=4),
                    expected,
                )

    def test_tabwidth_8(self):
        #        (line, (raw, effective))
        tests = (('no spaces', (0, 0)),
                 # Internal space isn't counted.
                 ('        space test', (8, 8)),
                 ('\ttab test', (1, 8)),
                 ('\t\tdouble tabs test', (2, 16)),
                 # Different results when mixing tabs and spaces.
                 ('        \tmixed test', (9, 16)),
                 ('      \t  mixed test', (9, 10)),
                 ('\t        mixed test', (9, 16)),
                 # Spaces not divisible by tabwidth.
                 ('  \tmixed test', (3, 8)),
                 (' \t mixed test', (3, 9)),
                 ('\t  mixed test', (3, 10)),
                 # Only checks spaces and tabs.
                 ('\nnewline test', (0, 0)))

        for line, expected in tests:
            with self.subTest(line=line):
                self.assertEqual(
                    editor.get_line_indent(line, tabwidth=8),
                    expected,
                )


def insert(text, string):
    text.delete('1.0', 'end')
    text.insert('end', string)
    text.update()  # Force update for colorizer to finish.


class IndentAndNewlineTest(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        requires('gui')
        cls.root = Tk()
        cls.root.withdraw()
        cls.window = Editor(root=cls.root)
        cls.window.indentwidth = 2
        cls.window.tabwidth = 2

    @classmethod
    def tearDownClass(cls):
        cls.window._close()
        del cls.window
        cls.root.update_idletasks()
        for id in cls.root.tk.call('after', 'info'):
            cls.root.after_cancel(id)
        cls.root.destroy()
        del cls.root

    def test_indent_and_newline_event(self):
        eq = self.assertEqual
        w = self.window
        text = w.text
        get = text.get
        nl = w.newline_and_indent_event

        TestInfo = namedtuple('Tests', ['label', 'text', 'expected', 'mark'])

        tests = (TestInfo('Empty line inserts with no indent.',
                          '  \n  def __init__(self):',
                          '\n  \n  def __init__(self):\n',
                          '1.end'),
                 TestInfo('Inside bracket before space, deletes space.',
                          '  def f1(self, a, b):',
                          '  def f1(self,\n         a, b):\n',
                          '1.14'),
                 TestInfo('Inside bracket after space, deletes space.',
                          '  def f1(self, a, b):',
                          '  def f1(self,\n         a, b):\n',
                          '1.15'),
                 TestInfo('Inside string with one line - no indent.',
                          '  """Docstring."""',
                          '  """Docstring.\n"""\n',
                          '1.15'),
                 TestInfo('Inside string with more than one line.',
                          '  """Docstring.\n  Docstring Line 2"""',
                          '  """Docstring.\n  Docstring Line 2\n  """\n',
                          '2.18'),
                 TestInfo('Backslash with one line.',
                          'a =\\',
                          'a =\\\n  \n',
                          '1.end'),
                 TestInfo('Backslash with more than one line.',
                          'a =\\\n          multiline\\',
                          'a =\\\n          multiline\\\n          \n',
                          '2.end'),
                 TestInfo('Block opener - indents +1 level.',
                          '  def f1(self):\n    pass',
                          '  def f1(self):\n    \n    pass\n',
                          '1.end'),
                 TestInfo('Block closer - dedents -1 level.',
                          '  def f1(self):\n    pass',
                          '  def f1(self):\n    pass\n  \n',
                          '2.end'),
                 )

        for test in tests:
            with self.subTest(label=test.label):
                insert(text, test.text)
                text.mark_set('insert', test.mark)
                nl(event=None)
                eq(get('1.0', 'end'), test.expected)

        # Selected text.
        insert(text, '  def f1(self, a, b):\n    return a + b')
        text.tag_add('sel', '1.17', '1.end')
        nl(None)
        # Deletes selected text before adding new line.
        eq(get('1.0', 'end'), '  def f1(self, a,\n         \n    return a + b\n')


class BackspaceTest(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        requires('gui')
        cls.root = Tk()
        cls.root.withdraw()
        cls.window = Editor(root=cls.root)

    @classmethod
    def tearDownClass(cls):
        cls.window._close()
        del cls.window
        cls.root.update_idletasks()
        for id in cls.root.tk.call('after', 'info'):
            cls.root.after_cancel(id)
        cls.root.destroy()
        del cls.root

    @staticmethod
    def perform_backspace(func):
        @functools.wraps(func)
        @run_in_tk_mainloop
        def new_test_method(self):
            text = self.window.text
            before, position, after = func(self)
            text.delete('1.0', 'end')
            text.insert('insert', before)
            text.mark_set('insert', '%s.%d' % (text.index('insert').split('.')[0], position))
            text.event_generate('<<smart-backspace>>')
            yield
            self.assertEqual(text.get('insert linestart', 'insert lineend'),
                             after)

        return new_test_method
    
    @perform_backspace
    def test_empty(self):
        return '', 0, ''

    @perform_backspace
    def test_just_whitespace_0(self):
        return ' '*8, 8, ' '*4

    @perform_backspace
    def test_just_whitespace_1(self):
        return ' '*8, 7, ' '*5

    @perform_backspace
    def test_just_whitespace_2(self):
        return ' '*8, 4, ' '*4

    @perform_backspace
    def test_indent_0(self):
        return ' '*8 + 'a', 8, ' '*4 + 'a'

    @perform_backspace
    def test_indent_1(self):
        return ' '*8 + 'a', 7, ' '*5 + 'a'
    
    @perform_backspace
    def test_normal_char_0(self):
        return ' '*8 + 'a'*4, 12, ' '*8 + 'a'*3

    @perform_backspace
    def test_normal_char_1(self):
        return ' '*8 + 'a'*4, 11, ' '*8 + 'a'*3

    @perform_backspace
    def test_surrounded_whitespace_0(self):
        return 'aa  a', 4, 'aa a'

    @perform_backspace
    def test_surrounded_whitespace_1(self):
        return '    a   a  ', 8, '    a  a  '

    @perform_backspace
    def test_trailing_0(self):
        return 'a' + ' '*7, 8, 'a'

    @perform_backspace
    def test_trailing_1(self):
        return ' '*4 + 'a' + ' '*7, 12, ' '*4 + 'a'

    @perform_backspace
    def test_trailing_2(self):
        return ' '*4 + 'a' + ' '*7, 10, ' '*4 + 'a' + ' '*2


class RMenuTest(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        requires('gui')
        cls.root = Tk()
        cls.root.withdraw()
        cls.window = Editor(root=cls.root)

    @classmethod
    def tearDownClass(cls):
        cls.window._close()
        del cls.window
        cls.root.update_idletasks()
        for id in cls.root.tk.call('after', 'info'):
            cls.root.after_cancel(id)
        cls.root.destroy()
        del cls.root

    class DummyRMenu:
        def tk_popup(x, y): pass

    def test_rclick(self):
        pass


if __name__ == '__main__':
    unittest.main(verbosity=2)
