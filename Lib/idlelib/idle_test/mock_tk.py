"""Classes that replace tkinter gui objects used by an object being tested.

A gui object is anything with a master or parent parameter, which is
typically required in spite of what the doc strings say.
"""
from bisect import bisect_left, bisect_right
import re


class Event:
    '''Minimal mock with attributes for testing event handlers.

    This is not a gui object, but is used as an argument for callbacks
    that access attributes of the event passed. If a callback ignores
    the event, other than the fact that is happened, pass 'event'.

    Keyboard, mouse, window, and other sources generate Event instances.
    Event instances have the following attributes: serial (number of
    event), time (of event), type (of event as number), widget (in which
    event occurred), and x,y (position of mouse). There are other
    attributes for specific events, such as keycode for key events.
    tkinter.Event.__doc__ has more but is still not complete.
    '''
    def __init__(self, **kwds):
        "Create event with attributes needed for test"
        self.__dict__.update(kwds)

class Var:
    "Use for String/Int/BooleanVar: incomplete"
    def __init__(self, master=None, value=None, name=None):
        self.master = master
        self.value = value
        self.name = name
    def set(self, value):
        self.value = value
    def get(self):
        return self.value

class Mbox_func:
    """Generic mock for messagebox functions, which all have the same signature.

    Instead of displaying a message box, the mock's call method saves the
    arguments as instance attributes, which test functions can then examine.
    The test can set the result returned to ask function
    """
    def __init__(self, result=None):
        self.result = result  # Return None for all show funcs
    def __call__(self, title, message, *args, **kwds):
        # Save all args for possible examination by tester
        self.title = title
        self.message = message
        self.args = args
        self.kwds = kwds
        return self.result  # Set by tester for ask functions

class Mbox:
    """Mock for tkinter.messagebox with an Mbox_func for each function.

    This module was 'tkMessageBox' in 2.x; hence the 'import as' in  3.x.
    Example usage in test_module.py for testing functions in module.py:
    ---
from idlelib.idle_test.mock_tk import Mbox
import module

orig_mbox = module.tkMessageBox
showerror = Mbox.showerror  # example, for attribute access in test methods

class Test(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        module.tkMessageBox = Mbox

    @classmethod
    def tearDownClass(cls):
        module.tkMessageBox = orig_mbox
    ---
    For 'ask' functions, set func.result return value before calling the method
    that uses the message function. When tkMessageBox functions are the
    only gui alls in a method, this replacement makes the method gui-free,
    """
    askokcancel = Mbox_func()     # True or False
    askquestion = Mbox_func()     # 'yes' or 'no'
    askretrycancel = Mbox_func()  # True or False
    askyesno = Mbox_func()        # True or False
    askyesnocancel = Mbox_func()  # True, False, or None
    showerror = Mbox_func()    # None
    showinfo = Mbox_func()     # None
    showwarning = Mbox_func()  # None

from _tkinter import TclError

class Misc:
    def destroy(self):
        pass

    def configure(self, cnf=None, **kw):
        pass

    config = configure

    def cget(self, key):
        pass

    __getitem__ = cget

    def __setitem__(self, key, value):
        self.configure({key: value})

    def bind(sequence=None, func=None, add=None):
        "Bind to this widget at event sequence a call to function func."
        pass

    def unbind(self, sequence, funcid=None):
        """Unbind for this widget for event SEQUENCE  the
        function identified with FUNCID."""
        pass

    def event_add(self, virtual, *sequences):
        """Bind a virtual event VIRTUAL (of the form <<Name>>)
        to an event SEQUENCE such that the virtual event is triggered
        whenever SEQUENCE occurs."""
        pass

    def event_delete(self, virtual, *sequences):
        """Unbind a virtual event VIRTUAL from SEQUENCE."""
        pass

    def event_generate(self, sequence, **kw):
        """Generate an event SEQUENCE. Additional
        keyword arguments specify parameter of the event
        (e.g. x, y, rootx, rooty)."""
        pass

    def event_info(self, virtual=None):
        """Return a list of all virtual events or the information
        about the SEQUENCE bound to the virtual event VIRTUAL."""
        pass

    def focus_set(self):
        pass


class Widget(Misc):
    def pack_configure(self, cnf={}, **kw):
        pass
    pack = configure = config = pack_configure

    def pack_forget(self):
        pass
    forget = pack_forget

    def pack_info(self):
        pass
    info = pack_info


class YView:
    """Mix-in class for querying and changing the vertical position
    of a widget's window."""

    def yview(self, *args):
        """Query and change the vertical position of the view."""
        pass

    def yview_moveto(self, fraction):
        """Adjusts the view in the window so that FRACTION of the
        total height of the canvas is off-screen to the top."""
        pass

    def yview_scroll(self, number, what):
        """Shift the y-view according to NUMBER which is measured in
        "units" or "pages" (WHAT)."""
        pass


class Text(Widget, YView):
    """A semi-functional non-gui replacement for tkinter.Text text editors.

    The mock's data model is that a text is a list of \n-terminated lines.
    The mock adds an empty string at  the beginning of the list so that the
    index of actual lines start at 1, as with Tk. The methods never see this.
    Tk initializes files with a terminal \n that cannot be deleted. It is
    invisible in the sense that one cannot move the cursor beyond it.

    This class is only tested (and valid) with strings of ascii chars.
    For testing, we are not concerned with Tk Text's treatment of,
    for instance, 0-width characters or character + accent.
   """
    def __init__(self, master=None, cnf={}, **kw):
        '''Initialize mock, non-gui, text-only Text widget.

        At present, all args are ignored. Almost all affect visual behavior.
        There are just a few Text-only options that affect text behavior.
        '''
        self.data = ['', '\n']
        self.marks = {'insert': (1, 0)}

    def index(self, index):
        "Return string version of index decoded according to current text."
        return "%s.%s" % self._decode(index, endflag=1)

    def _decode(self, index, endflag=0):
        """Return a (line, char) tuple of int indexes into self.data.

        This implements .index without converting the result back to a string.
        The result is constrained by the number of lines and linelengths of
        self.data. For many indexes, the result is initially (1, 0).

        The input index may have any of several possible forms:
        * line.char float: converted to 'line.char' string;
        * 'line.char' string, where line and char are decimal integers;
        * 'line.char lineend', where lineend='lineend' (and char is ignored);
        * 'line.end', where end='end' (same as above);
        * 'insert', the positions before terminal \n;
        * 'end', whose meaning depends on the endflag passed to ._index.
        * 'sel.first' or 'sel.last', where sel is a tag -- not implemented.
        """
        if isinstance(index, (float, bytes)):
            index = str(index)
        try:
            index = index.lower().strip()
        except AttributeError:
            raise TclError('bad text index "%s"' % index) from None

        def clamp(value, min_val, max_val):
            return max(min_val, min(max_val, value))

        lastline = len(self.data) - 1  # same as number of text lines

        first_part = re.match(r'^[^-+\s]+', index).group(0)

        if first_part in self.marks:
            line, char = self.marks[first_part]
        elif first_part == 'end':
            line, char = self._endex(endflag)
        else:
            line_str, char_str = first_part.split('.')
            line = int(line_str)

            # Out of bounds line becomes first or last ('end') index
            if line < 1:
                line, char = 1, 0
            elif line > lastline:
                line, char = self._endex(endflag)
            else:
                linelength = len(self.data[line])
                if char_str == 'end':
                    char = linelength - 1
                else:
                    char = clamp(int(char_str), 0, linelength - 1)

        part_matches = list(re.finditer(r'''
            (?:
                ([-+]\s*[1-9][0-9]*)
                \s*
                (?:(display|any)\s+)?
                (chars|char|cha|ch|c|
                 indices|indice|indic|indi|ind|in|i|
                 lines|line|lin|li|l)
            |
                linestart|lineend|wordstart|wordend
            )
            (?=[-+]|\s|$)
            ''', index[len(first_part):], re.VERBOSE))

        for m in part_matches:
            part = m.group(0)

            if part == 'lineend':
                linelength = len(self.data[line]) - 1
                char = linelength
            elif part == 'linestart':
                char = 0
            elif part == 'wordstart':
                raise NotImplementedError
            elif part == 'wordend':
                raise NotImplementedError
            else:
                number, submod, type = m.groups()
                delta = int(number)
                if type[0] in ('c', 'i'):
                    # chars / indices
                    char += delta
                    if char < 0:
                        while line > 0:
                            line -= 1
                            char += len(self.data[line])
                    elif char >= len(self.data[line]):
                        while line < lastline:
                            char -= len(self.data[line])
                else:
                    assert type[0] == 'l'
                    # lines
                    line += delta
                    line = clamp(line, 1, lastline)
                linelength = len(self.data[line])
                char = clamp(char, 0, linelength - 1)

        return line, char

    def _endex(self, endflag):
        """Return position for 'end' or line overflow corresponding to endflag.

        -1: position before terminal \n; for .insert(), .delete
        0: position after terminal \n; for .get, .delete index 1
        1: same viewed as beginning of non-existent next line (for .index)
        """
        n = len(self.data)
        if endflag == 1:
            return n, 0
        else:
            n -= 1
            return n, len(self.data[n]) + endflag

    def insert(self, index, chars):
        "Insert chars before the character at index."

        if not chars:  # ''.splitlines() is [], not ['']
            return
        chars = chars.splitlines(True)
        if chars[-1][-1] == '\n':
            chars.append('')
        line, char = self._decode(index, -1)
        before = self.data[line][:char]
        after = self.data[line][char:]
        self.data[line] = before + chars[0]
        self.data[line+1:line+1] = chars[1:]
        self.data[line+len(chars)-1] += after

        for mark in list(self.marks):
            mark_line, mark_char = self.marks[mark]
            if (
                    (mark_line, mark_char) > (line, char) or
                    mark == 'insert' and (mark_line, mark_char) == (line, char)
            ):
                new_line = mark_line + len(chars) - 1
                new_char = mark_char + len(chars[-1])
                if mark_line > line:
                    new_char -= char
                self.marks[mark] = (new_line, new_char)

    def get(self, index1, index2=None):
        "Return slice from index1 to index2 (default is 'index1+1')."

        startline, startchar = self._decode(index1)
        if index2 is None:
            endline, endchar = startline, startchar+1
        else:
            endline, endchar = self._decode(index2)

        if startline == endline:
            return self.data[startline][startchar:endchar]
        else:
            lines = [self.data[startline][startchar:]]
            for i in range(startline+1, endline):
                lines.append(self.data[i])
            lines.append(self.data[endline][:endchar])
            return ''.join(lines)

    def delete(self, index1, index2=None):
        '''Delete slice from index1 to index2 (default is 'index1+1').

        Adjust default index2 ('index+1) for line ends.
        Do not delete the terminal \n at the very end of self.data ([-1][-1]).
        '''
        startline, startchar = self._decode(index1, -1)
        if index2 is None:
            if startchar < len(self.data[startline])-1:
                # not deleting \n
                endline, endchar = startline, startchar+1
            elif startline < len(self.data) - 1:
                # deleting non-terminal \n, convert 'index1+1 to start of next line
                endline, endchar = startline+1, 0
            else:
                # do not delete terminal \n if index1 == 'insert'
                return
        else:
            endline, endchar = self._decode(index2, -1)
            # restricting end position to insert position excludes terminal \n

        if startline == endline and startchar < endchar:
            self.data[startline] = self.data[startline][:startchar] + \
                                             self.data[startline][endchar:]
        elif startline < endline:
            self.data[startline] = self.data[startline][:startchar] + \
                                   self.data[endline][endchar:]
            del self.data[startline+1:endline+1]

        for mark in list(self.marks):
            mark_line, mark_char = self.marks[mark]
            if (mark_line, mark_char) > (startline, startchar):
                if (mark_line, mark_char) <= (endline, endchar):
                    (new_line, new_char) = (startline, startchar)
                elif mark_line == endline:
                    new_line = startline
                    new_char = startchar + (mark_char - endchar)
                else:  # mark_line > endline
                    new_line = mark_line - (endline - startline)
                    new_char = mark_char
                self.marks[mark] = (new_line, new_char)

    def compare(self, index1, op, index2):
        line1, char1 = self._decode(index1)
        line2, char2 = self._decode(index2)
        if op == '<':
            return line1 < line2 or line1 == line2 and char1 < char2
        elif op == '<=':
            return line1 < line2 or line1 == line2 and char1 <= char2
        elif op == '>':
            return line1 > line2 or line1 == line2 and char1 > char2
        elif op == '>=':
            return line1 > line2 or line1 == line2 and char1 >= char2
        elif op == '==':
            return line1 == line2 and char1 == char2
        elif op == '!=':
            return line1 != line2 or  char1 != char2
        else:
            raise TclError('''bad comparison operator "%s": '''
                                  '''must be <, <=, ==, >=, >, or !=''' % op)

    # The following Text methods normally do something and return None.
    # Whether doing nothing is sufficient for a test will depend on the test.

    def mark_set(self, name, index):
        "Set mark *name* before the character at index."
        self.marks[name] = self._decode(index)

    def mark_unset(self, *markNames):
        "Delete all marks in markNames."
        for name in markNames:
            if name == 'end' or '.' in name:
                raise ValueError(f"Invalid mark name: {name}")
            del self.marks[name]

    def tag_remove(self, tagName, index1, index2=None):
        "Remove tag tagName from all characters between index1 and index2."
        pass

    def tag_prevrange(self, tagName, index1, index2=None):
        return ()

    # The following Text methods affect the graphics screen and return None.
    # Doing nothing should always be sufficient for tests.

    def scan_dragto(self, x, y):
        "Adjust the view of the text according to scan_mark"

    def scan_mark(self, x, y):
        "Remember the current X, Y coordinates."

    def see(self, index):
        "Scroll screen to make the character at INDEX is visible."
        pass


class Entry:
    "Mock for tkinter.Entry."
    def focus_set(self):
        pass


class Listbox(Widget, YView):
    def __init__(self, master=None, cnf={}, **kw):
        self._items = []
        self._selection = []

    def _normalize_first_last(self, first, last):
        first = self._index(first)
        last = first if last is None else self._index(last)
        if not (0 <= first < len(self._items)):
            raise IndexError()
        if not (0 <= last < len(self._items)):
            raise IndexError()
        return first, last

    def _index(self, index, end_after_last=False):
        if index == 'end':
            index = len(self._items) - (0 if end_after_last else 1)
        elif index in ('active', 'anchor'):
            raise NotImplementedError()
        elif isinstance(index, str) and index.startswith('@'):
            raise NotImplementedError()
        else:
            if not isinstance(index, int):
                raise ValueError()
        return index

    def curselection(self):
        """Return the indices of currently selected item."""
        return list(self._selection)

    def delete(self, first, last=None):
        """Delete items from FIRST to LAST (included)."""
        first, last = self._normalize_first_last(first, last)

        if last < first:
            return
        self.selection_clear(first, last)
        self._items[first:last+1] = []
        sel_idx = bisect_left(self._selection, first)
        for i in range(sel_idx, len(self._selection)):
            self._selection[i] -= (last - first + 1)

    def get(self, first, last=None):
        """Get list of items from FIRST to LAST (included)."""
        first, last = self._normalize_first_last(first, last)

        if last < first:
            return []
        return self._items[first:last + 1]

    def index(self, index):
        """Return index of item identified with INDEX."""
        index = self._index(index, end_after_last=True)
        if not index >= 0:
            raise IndexError
        if index > len(self._items):
            index = len(self._items)
        return index

    def insert(self, index, *elements):
        """Insert ELEMENTS at INDEX."""
        index = self._index(index, end_after_last=True)
        if not index >= 0:
            raise IndexError
        self._items[index:index] = list(elements)
        sel_index = bisect_left(self._selection, index)
        for i in range(sel_index, len(self._selection)):
            self._selection[i] += len(elements)
        return ""

    def see(self, index):
        """Scroll such that INDEX is visible."""
        index = self._index(index)
        pass

    def selection_clear(self, first, last=None):
        """Clear the selection from FIRST to LAST (included)."""
        first, last = self._normalize_first_last(first, last)

        if last < first:
            return []
        first_sel_idx = bisect_left(self._selection, first)
        last_sel_idx = bisect_right(self._selection, last)
        self._selection[first_sel_idx:last_sel_idx] = []

    select_clear = selection_clear

    def selection_includes(self, index):
        """Return 1 if INDEX is part of the selection."""
        index = self._index(index)
        if not (0 <= index < len(self._items)):
            raise IndexError()
        return index in self._selection

    select_includes = selection_includes

    def selection_set(self, first, last=None):
        """Set the selection from FIRST to LAST (included) without
        changing the currently selected elements."""
        first, last = self._normalize_first_last(first, last)

        if last < first:
            return []
        first_sel_idx = bisect_left(self._selection, first)
        last_sel_idx = bisect_right(self._selection, last)
        self._selection[first_sel_idx:last_sel_idx] = list(range(first, last+1))

    select_set = selection_set

    def size(self):
        """Return the number of elements in the listbox."""
        return len(self._items)
