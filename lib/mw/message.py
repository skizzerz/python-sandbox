import sandbox as sb
import mw
import mw.language

__all__ = ["Message", "newFallbackSequence", "newRawMessage", "RawParam",
           "NumParam", "getDefaultLanguage"]

class Message:
    def __init__(self, key, *params):
        self._data = {
            "raw": False,
            "useDB": True,
            "lang": None,
            "key": key,
            "params": []
        }

        self.params(params)

    def params(self, *params):
        if params:
            self._data["params"].extend(params)

        return self

    def rawParams(self, *params):
        return self.params(*(RawParam(p) for p in params))

    def numParams(self, *params):
        return self.params(*(NumParam(p) for p in params))

    def inLanguage(self, lang):
        if isinstance(lang, mw.language.Language):
            self._data["lang"] = lang.getCode()
        else:
            self._data["lang"] = lang

    def useDatabase(self, value):
        self._data["useDB"] = value
        return self

    def plain(self):
        return sb.trampoline("message_plain", self._data)

    def exists(self):
        try:
            self.plain()
            return True
        except KeyError:
            return False

    def isBlank(self):
        try:
            msg = self.plain()
            return msg == ""
        except KeyError:
            return True

    def isDisabled(self):
        try:
            msg = self.plain()
            return msg == "" or msg == "-"
        except KeyError:
            return True

class RawParam:
    def __init__(self, param):
        self.raw = True
        self.param = param

class NumParam:
    def __init__(self, param):
        self.num = True
        self.param = param

def newFallbackSequence(*msgs):
    for key in msgs:
        msg = Message(key)
        if msg.exists():
            return msg

    return None

def newRawMessage(msg, *params):
    obj = Message(msg, *params)
    obj._data["raw"] = True
    return obj

def getDefaultLanguage():
    return mw.language.default
