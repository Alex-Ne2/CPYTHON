# -*- coding: windows-1252 -*-

from gettext import gettext as _


# ascii text
_('foo')

# windows-1252 text
_('� �')

# non-windows-1252 text
_('\u03b1 \u03b2')
