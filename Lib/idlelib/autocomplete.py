"""Complete either attribute names or file names.

Either on demand or after a user-selected delay after a key character,
pop up a list of candidates.
"""
import __main__
import itertools
import os
import re
import string
import sys

# Two types of completions; defined here for autocomplete_w import below.
ATTRS, FILES, DICTKEYS = 0, 1, 2
from idlelib import autocomplete_w
from idlelib.config import idleConf
from idlelib.hyperparser import HyperParser

# Tuples passed to open_completions.
#       EvalFunc, Complete, Mode
FORCE = True,     False,    None      # Control-Space.
TAB   = False,    True,     None      # Tab.
TRY_A = False,    False,    ATTRS     # '.' for attributes.
TRY_D = False,    False,    DICTKEYS  # '[' for dict keys.
TRY_F = False,    False,    FILES     # '/' in quotes for file name.

# This string includes all chars that may be in an identifier.
# TODO Update this here and elsewhere.
ID_CHARS = string.ascii_letters + string.digits + "_"

SEPS = f"{os.sep}{os.altsep if os.altsep else ''}"
TRIGGERS = f".[{SEPS}"

class AutoComplete:

    def __init__(self, editwin=None):
        self.editwin = editwin
        if editwin is not None:   # not in subprocess or no-gui test
            self.text = editwin.text
        self.autocompletewindow = None
        # id of delayed call, and the index of the text insert when
        # the delayed call was issued. If _delayed_completion_id is
        # None, there is no delayed call.
        self._delayed_completion_id = None
        self._delayed_completion_index = None

        self._make_dict_key_reprs = self._make_dict_key_repr_func()

    @classmethod
    def reload(cls):
        cls.popupwait = idleConf.GetOption(
            "extensions", "AutoComplete", "popupwait", type="int", default=0)

    def _make_autocomplete_window(self):  # Makes mocking easier.
        return autocomplete_w.AutoCompleteWindow(self.text)

    def _remove_autocomplete_window(self, event=None):
        if self.autocompletewindow:
            self.autocompletewindow.hide_window()
            self.autocompletewindow = None

    def force_open_completions_event(self, event):
        "(^space) Open completion list, even if a function call is needed."
        self.open_completions(FORCE)
        return "break"

    def autocomplete_event(self, event):
        "(tab) Complete word or open list if multiple options."
        if hasattr(event, "mc_state") and event.mc_state or\
                not self.text.get("insert linestart", "insert").strip():
            # A modifier was pressed along with the tab or
            # there is only previous whitespace on this line, so tab.
            return None
        if self.autocompletewindow and self.autocompletewindow.is_active():
            self.autocompletewindow.complete()
            return "break"
        else:
            opened = self.open_completions(TAB)
            return "break" if opened else None

    def try_open_completions_event(self, event=None):
        r"""(./\[) Open completion list after pause with no movement."""
        lastchar = self.text.get("insert-1c")
        if lastchar in TRIGGERS:
            args = {".": TRY_A, "[": TRY_D}.get(lastchar, TRY_F)
            self._delayed_completion_index = self.text.index("insert")
            if self._delayed_completion_id is not None:
                self.text.after_cancel(self._delayed_completion_id)
            self._delayed_completion_id = self.text.after(
                self.popupwait, self._delayed_open_completions, args)

    def _delayed_open_completions(self, args):
        "Call open_completions if index unchanged."
        self._delayed_completion_id = None
        if self.text.index("insert") == self._delayed_completion_index:
            self.open_completions(args)

    _dict_str_key_prefix_re = re.compile(r'\s*(br|rb|fr|rf|b|f|r|u|)', re.I)

    @classmethod
    def _is_completing_dict_key(cls, hyper_parser):
        hp = hyper_parser
        if hp.is_in_string():
            # hp.indexbracket is an opening quote;
            # check if the bracket before it is '['.
            if hp.indexbracket == 0:
                return None
            opening_bracket_idx = hp.bracketing[hp.indexbracket - 1][0]
            if hp.rawtext[opening_bracket_idx] != '[':
                return None
            # The completion start is everything from the '[', except initial
            # whitespace. Check that it's a valid beginning of a str/bytes
            # literal.
            opening_quote_idx = hp.bracketing[hp.indexbracket][0]
            m = cls._dict_str_key_prefix_re.fullmatch(
                hp.rawtext[opening_bracket_idx+1:opening_quote_idx])
            if m is None:
                return None
            comp_start = \
                m.group(1) + hp.rawtext[opening_quote_idx:hp.indexinrawtext]
        elif hp.is_in_code():
            # This checks the the last bracket is an opener,
            # and also avoids an edge-case exception.
            if not hp.isopener[hp.indexbracket]:
                return None
            # Check if the last opening bracket a '['.
            opening_bracket_idx = hp.bracketing[hp.indexbracket][0]
            if hp.rawtext[opening_bracket_idx] != '[':
                return None
            # We want to complete str/bytes dict keys only if the existing
            # completion start could be the beginning of a literal. Note
            # that an empty or all-whitespace completion start is acceptable.
            m = cls._dict_str_key_prefix_re.fullmatch(
                hp.rawtext[opening_bracket_idx + 1:hp.indexinrawtext])
            if m is None:
                return None
            comp_start = m.group(1)
        else:
            return None

        hp.set_index("insert-%dc" % (hp.indexinrawtext - opening_bracket_idx))
        comp_what = hp.get_expression()
        return comp_what, comp_start

    @classmethod
    def _make_dict_key_repr_func(cls):
        # Prepare escaping utilities.
        escape_re = re.compile(r'[\0-\15]')
        escapes = {ord_: f'\\x{ord_:02x}' for ord_ in range(14)}
        escapes.update({0: '\\0', 9: '\\t', 10: '\\n', 13: '\\r'})
        def control_char_escape(match):
            return escapes[ord(match.group())]
        high_byte_escape_re = re.compile(r'[\x80-\xff]')
        def high_byte_sub(match):
            """escaping for bytes values above 127"""
            return f'\\x{ord(match.group()):x}'
        non_bmp_re = re.compile(r'[\U00010000-\U0010ffff]')
        def non_bmp_sub(match):
            """escaping for non-BMP unicode code points"""
            return f'\\U{ord(match.group()):08x}'

        def str_reprs(str_comps, prefix, is_raw, quote_type):
            repr_prefix = prefix.replace('b', '').replace('B', '')
            repr_template = f"{repr_prefix}{quote_type}{{}}{quote_type}"
            if not is_raw:
                # Escape back-slashes.
                str_comps = [x.replace('\\', '\\\\') for x in str_comps]
            # Escape quotes.
            str_comps = [x.replace(quote_type, '\\' + quote_type) for x in
                         str_comps]
            if len(quote_type) == 3:
                # With triple-quotes, we need to escape the final character if
                # it is a single quote of the same kind.
                str_comps = [
                    (x[:-1] + '\\' + x[-1]) if x[-1:] == quote_type[0] else x
                    for x in str_comps
                ]
            if not is_raw:
                # Escape control characters.
                str_comps = [escape_re.sub(control_char_escape, x)
                             for x in str_comps]
                str_comps = [non_bmp_re.sub(non_bmp_sub, x) for x in str_comps]
                str_comps = [repr_template.format(x) for x in str_comps]
            else:
                # Format as raw literals (r"...") except when there are control
                # characters which must be escaped.
                non_raw_prefix = repr_prefix.replace('r', '').replace('R', '')
                non_raw_template = \
                    f"{non_raw_prefix}{quote_type}{{}}{quote_type}"
                str_comps = [
                    repr_template.format(x)
                    if escape_re.search(x) is None and non_bmp_re.search(
                        x) is None
                    else non_raw_template.format(
                        non_bmp_re.sub(non_bmp_sub,
                                       escape_re.sub(control_char_escape,
                                                     x)))
                    for x in str_comps
                ]
            return str_comps

        def bytes_reprs(bytes_comps, prefix, is_raw, quote_type):
            repr_prefix = re.sub(r'[fu]', '', prefix, flags=re.I)
            if 'b' not in repr_prefix.lower():
                repr_prefix += 'b'

            repr_template = f"{repr_prefix}{quote_type}{{}}{quote_type}"
            comps = (x.decode('latin-1') for x in bytes_comps)
            if not is_raw:
                # Escape back-slashes.
                comps = [x.replace('\\', '\\\\') for x in comps]
            # Escape quotes.
            comps = [x.replace(quote_type, '\\' + quote_type) for x in comps]
            if not is_raw:
                # Escape control characters.
                comps = [
                    escape_re.sub(control_char_escape, x) for x in comps]
                # Escape high-value characters.
                comps = [
                    high_byte_escape_re.sub(high_byte_sub, x) for x in comps]
                comps = [repr_template.format(x) for x in comps]
            else:
                # Format as raw literals (br"...") except when there are
                # control characters or high-value characters which must be
                # escaped.
                non_raw_template = f"b{quote_type}{{}}{quote_type}"
                comps = [
                    repr_template.format(x)
                    if (
                            escape_re.search(x) is None and
                            high_byte_escape_re.search(x) is None
                    )
                    else non_raw_template.format(
                        high_byte_escape_re.sub(
                            high_byte_sub,
                            escape_re.sub(control_char_escape, x))
                    )
                    for x in comps
                ]

            return comps

        def analyze_prefix_and_quote_type(comp_start):
            """Analyze existing prefix and quote type of a completion start"""
            is_raw = False
            quote_type = '"'
            prefix_match = cls._dict_str_key_prefix_re.match(comp_start)
            if prefix_match is not None:
                prefix = prefix_match.group(1)
                post_prefix = comp_start[prefix_match.end():]

                is_raw = 'r' in prefix.lower()
                if post_prefix:
                    quote_type_candidate = post_prefix[0]
                    if quote_type_candidate not in "'\"":
                        pass  # use the default quote type
                    elif post_prefix[1:2] == quote_type_candidate:
                        quote_type = quote_type_candidate * 3
                    else:
                        quote_type = quote_type_candidate
            else:
                prefix = ''

            return prefix, is_raw, quote_type

        def _dict_key_reprs(comp_start, comp_list):
            """Customized repr() for str and bytes.

            This retains the prefix and quote type of the completed partial
            str/bytes literal, if they exist.
            """
            prefix, is_raw, quote_type = \
                analyze_prefix_and_quote_type(comp_start)

            str_comps = [c for c in comp_list if type(c) == str]
            bytes_comps = [c for c in comp_list if type(c) == bytes]

            str_comps = str_reprs(str_comps, prefix, is_raw, quote_type)
            bytes_comps = bytes_reprs(bytes_comps, prefix, is_raw, quote_type)

            return list(itertools.chain(str_comps, bytes_comps))

        return _dict_key_reprs

    def open_completions(self, args):
        """Find the completions and create the AutoCompleteWindow.
        Return True if successful (no syntax error or so found).
        If complete is True, then if there's nothing to complete and no
        start of completion, won't open completions and return False.
        If mode is given, will open a completion list only in this mode.
        """
        evalfuncs, complete, mode = args
        # Cancel another delayed call, if it exists.
        if self._delayed_completion_id is not None:
            self.text.after_cancel(self._delayed_completion_id)
            self._delayed_completion_id = None

        hp = HyperParser(self.editwin, "insert")
        curline = self.text.get("insert linestart", "insert")
        i = j = len(curline)

        if mode in (None, DICTKEYS):
            comp_what_and_start = self._is_completing_dict_key(hp)
            if comp_what_and_start is not None:
                comp_what, comp_start = comp_what_and_start
                if comp_what and (evalfuncs or '(' not in comp_what):
                    self._remove_autocomplete_window()
                    mode = DICTKEYS
                else:
                    return None
        if mode in (None, FILES) and hp.is_in_string():
            # Find the beginning of the string.
            # fetch_completions will look at the file system to determine
            # whether the string value constitutes an actual file name
            # XXX could consider raw strings here and unescape the string
            # value if it's not raw.
            self._remove_autocomplete_window()
            mode = FILES
            # Find last separator or string start
            while i and curline[i-1] not in "'\"" + SEPS:
                i -= 1
            comp_start = curline[i:j]
            j = i
            # Find string start
            while i and curline[i-1] not in "'\"":
                i -= 1
            comp_what = curline[i:j]
        elif mode in (None, ATTRS) and hp.is_in_code():
            self._remove_autocomplete_window()
            mode = ATTRS
            while i and (curline[i-1] in ID_CHARS or ord(curline[i-1]) > 127):
                i -= 1
            comp_start = curline[i:j]
            if i and curline[i-1] == '.':  # Need object with attributes.
                hp.set_index("insert-%dc" % (len(curline)-(i-1)))
                comp_what = hp.get_expression()
                if (not comp_what or
                   (not evalfuncs and comp_what.find('(') != -1)):
                    return None
            else:
                comp_what = ""
        elif mode == DICTKEYS:
            pass
        else:
            return None

        if complete and not comp_what and not comp_start:
            return None
        comp_lists = self.fetch_completions(comp_what, mode)
        if mode == DICTKEYS:
            assert comp_lists[0] == comp_lists[1]
            reprs = self._make_dict_key_reprs(comp_start, comp_lists[0])

            if hp.is_in_code():
                # Add variables in the global namespace to the comp lists.
                comp_lists = (reprs, list(reprs))
                extra_comp_lists = self.fetch_completions("", ATTRS)
                comp_lists[0].extend(extra_comp_lists[0])
                comp_lists[0].sort()
                comp_lists[1].extend(extra_comp_lists[1])
                comp_lists[1].sort()
            else:
                reprs.sort()
                comp_lists = (reprs, list(reprs))
        if not comp_lists[0]:
            return None
        self.autocompletewindow = self._make_autocomplete_window()
        return not self.autocompletewindow.show_window(
                comp_lists, "insert-%dc" % len(comp_start),
                complete, mode)

    def fetch_completions(self, what, mode):
        """Return a pair of lists of completions for something. The first list
        is a sublist of the second. Both are sorted.

        If there is a Python subprocess, get the comp. list there.  Otherwise,
        either fetch_completions() is running in the subprocess itself or it
        was called in an IDLE EditorWindow before any script had been run.

        The subprocess environment is that of the most recently run script.  If
        two unrelated modules are being edited some calltips in the current
        module may be inoperative if the module was not the last to run.
        """
        try:
            rpcclt = self.editwin.flist.pyshell.interp.rpcclt
        except:
            rpcclt = None
        if rpcclt:
            return rpcclt.remotecall("exec", "get_the_completion_list",
                                     (what, mode), {})
        else:
            if mode == ATTRS:
                if what == "":
                    namespace = {**__main__.__builtins__.__dict__,
                                 **__main__.__dict__}
                    bigl = eval("dir()", namespace)
                    bigl.sort()
                    if "__all__" in bigl:
                        smalll = sorted(eval("__all__", namespace))
                    else:
                        smalll = [s for s in bigl if s[:1] != '_']
                else:
                    try:
                        entity = self.get_entity(what)
                        bigl = dir(entity)
                        bigl.sort()
                        if "__all__" in bigl:
                            smalll = sorted(entity.__all__)
                        else:
                            smalll = [s for s in bigl if s[:1] != '_']
                    except:
                        return [], []

            elif mode == FILES:
                if what == "":
                    what = "."
                try:
                    expandedpath = os.path.expanduser(what)
                    bigl = os.listdir(expandedpath)
                    bigl.sort()
                    smalll = [s for s in bigl if s[:1] != '.']
                except OSError:
                    return [], []

            elif mode == DICTKEYS:
                try:
                    entity = self.get_entity(what)
                    keys = entity.keys() if isinstance(entity, dict) else []
                except:
                    return [], []
                smalll = bigl = [k for k in keys if type(k) in (bytes, str)]

            if not smalll:
                smalll = bigl
            return smalll, bigl

    def get_entity(self, name):
        "Lookup name in a namespace spanning sys.modules and __main.dict__."
        return eval(name, {**sys.modules, **__main__.__dict__})


AutoComplete.reload()

if __name__ == '__main__':
    from unittest import main
    main('idlelib.idle_test.test_autocomplete', verbosity=2)
