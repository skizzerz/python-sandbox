from sandbox import trampoline

__all__ = ["Frame", "allToString", "clone", "getCurrentFrame", "incrementExpensiveFunctionCount",
           "isSubsting", "loadData", "dumpObject", "log", "logObject"]

# Object representing an undefined value, for use in functions that have overloads
undefined = tuple()

# Cache of created Frame objects
_framecache = {}

# Create the Frame class, which is passed as an argument to all invoked functions
class Frame:
    def __init__(self, frame_id, title):
        self._frame_id = frame_id
        self._title = title
        self.args = FrameArgs(frame_id)

    # the lua API supports passing in a table as a single arg. We do not because
    # unpacking is super easy in python and it would add a lot of complexity
    # if we had to detect if we were passed a list/dict/whatever and act accordingly
    def callParserFunction(self, name, *args, **kwargs):
        wiki_args = self._getWikiArgs(args, kwargs)
        return trampoline("frame_callParserFunction", self._frame_id, name, wiki_args)

    def expandTemplate(self, title, *args, **kwargs):
        wiki_args = self._getWikiArgs(args, kwargs)
        return trampoline("frame_expandTemplate", self._frame_id, title, wiki_args)

    def extensionTag(self, name, content, *args, **kwargs):
        wiki_args = self._getWikiArgs(args, kwargs)
        return trampoline("frame_extensionTag", self._frame_id, name, content, wiki_args)

    def getParent(self):
        parent = _trampoline("frame_getParent", self._frame_id)
        if parent["id"] not in _framecache:
            _framecache[parent["id"]] = Frame(parent["id"], parent["title"])
        return _framecache[parent["id"]]

    def getTitle(self):
        return self.title

    def newChild(self, title, *args, **kwargs):
        wiki_args = self._getWikiArgs(args, kwargs)
        child = trampoline("frame_newChild", self._frame_id, wiki_args)
        f = Frame(child["id"], child["title"])
        _framecache[child["id"]] = f
        return f

    def preprocess(self, text):
        return trampoline("frame_preprocess", self._frame_id, text)

    def getArgument(self, name):
        arg = trampoline("frame_getArgument", self._frame_id, name)
        if arg is not None:
            arg = ArgumentValue(self, arg)
        return arg

    def newParserValue(self, text):
        return ParserValue(self, text)

    def newTemplateParserValue(self, title, *args, **kwargs):
        return TemplateParserValue(self, title, *args, **kwargs)

    def argumentPairs(self):
        return self.args.items()

    def _getWikiArgs(args=[], kwargs={}):
        # Arguments on-wiki start at 1 for numbered arguments, not 0
        wiki_args = {i + 1: v for i, v in args.items()}
        wiki_args.update(kwargs)
        return wiki_args

class FrameArgs:
    def __init__(self, frame_id):
        self._frame_id = frame_id

    def __getitem__(self, key):
        arg = trampoline("frame_getArgument", self._frame_id, key)
        if arg is None:
            raise KeyError
        return arg

    def __iter__(self):
        i = 0
        while True:
            arg = trampoline("frame_getArgumentByPos", self._frame_id, i)
            if arg is None:
                return
            i += 1
            yield arg

class ArgumentValue:
    def __init__(self, frame, text):
        self._frame = frame
        self._text = text

    def expand():
        return self._text

class ParserValue:
    def __init__(self, frame, text):
        self._frame = frame
        self._text = text

    def expand():
        return self._frame.preprocess(self._text)

class TemplateParserValue:
    def __init__(self, title, *args, **kwargs):
        self._title = title
        self._args = args
        self._kwargs = kwargs

    def expand():
        return self._frame.expandTemplate(title, *self._args, **self._kwargs)
