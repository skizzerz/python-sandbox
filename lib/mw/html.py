import mw

# TODO: shamelessly port the lua version of this to ensure the implementation remains consistent
class _HtmlBuilder:
    def __init__(self, tag_name, self_closing, parent=None):
        self._tag_name = tag_name
        self._self_closing = self_closing,
        self._parent = parent
        self._children = []
        self._attrs = {}
        self._classes = []
        self._styles = {}

    def __str__(self):
        pass

    def node(self, builder):
        if isinstance(builder, _HtmlBuilder):
            self._children.append(builder)
        return self

    def wikitext(self, *args):
        for text in args:
            if text is None:
                break
            self._children.append(str(text))
        return self

    def newline(self):
        self._children.append("\n")
        return self

    def tag(tag_name, self_closing=False):
        child = _HtmlBuilder(tag_name, self_closing=self_closing, parent=self)
        self._children.append(child)
        return child

    def attr(name, value=mw.undefined):
        if (value is mw.undefined):
            self._attrs = name
        elif value is None:
            del self._attrs[name]
        else:
            self._attrs[name] = value
        return self

    def getAttr(name):
        return self._attrs[name]


def create(tag_name, self_closing=False):
    return HtmlBuilder(tag_name, self_closing=self_closing)
