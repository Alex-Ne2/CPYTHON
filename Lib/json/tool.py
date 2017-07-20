r"""Command-line tool to validate and pretty-print JSON

Usage::

    $ echo '{"json":"obj"}' | python -m json.tool
    {
        "json": "obj"
    }
    $ echo '{ 1.2:3.4}' | python -m json.tool
    Expecting property name enclosed in double quotes: line 1 column 3 (char 2)

"""
import argparse
import collections
import json
import sys


def main():
    prog = 'python -m json.tool'
    description = ('A simple command line interface for json module '
                   'to validate and pretty-print JSON objects.')
    parser = argparse.ArgumentParser(prog=prog, description=description)
    parser.add_argument('infile', nargs='?', type=argparse.FileType(),
                        default=sys.stdin,
                        help='a JSON file to be validated or pretty-printed')
    parser.add_argument('outfile', nargs='?', type=argparse.FileType('w'),
                        default=sys.stdout,
                        help='write the output of infile to outfile')
    group = parser.add_mutually_exclusive_group()
    group.add_argument('--indent', default=4, type=int,
                       help='Indent level (number of spaces) for '
                       'pretty-printing. Defaults to 4.')
    group.add_argument('--no-indent', action='store_const', dest='indent',
                       const=None, help='Use compact mode.')
    parser.add_argument('--sort-keys', action='store_true',
                        help='sort the output of dictionaries by key')
    options = parser.parse_args()

    hook = collections.OrderedDict if options.sort_keys else None
    with options.infile as infile:
        try:
            obj = json.load(infile, object_pairs_hook=hook)
        except ValueError as e:
            raise SystemExit(e)

    with options.outfile as outfile:
        json.dump(obj, outfile,
                  indent=options.indent,
                  sort_keys=options.sort_keys,
                  )
        outfile.write('\n')


if __name__ == '__main__':
    main()
