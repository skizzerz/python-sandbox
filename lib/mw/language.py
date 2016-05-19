import sandbox as sb

__all__ = ["Language", "fetchLanguageName", "fetchLanguageNames",
           "content", "default", "getFallbacksFor", "isKnownLanguageTag",
           "isSupportedLanguage", "isValidBuiltinCode", "isValidCode"]

# Language objects for the default and content languages, instantiated by init.py
content = None
default = None

class Language:
    def __init__(self, code):
        self.code = code
        self._data = sb.trampoline("language_load", self.code)

    def getFallbackLanguages(self):
        return getFallbacksFor(self.code)

    def isRTL(self):
        return self._data["RTL"]

    def lc(self, s):
        return sb.trampoline("language_lc", self.code, s)

    def lcfirst(self, s):
        return sb.trampoline("language_lcfirst", self.code, s)

    def uc(self, s):
        return sb.trampoline("language_uc", self.code, s)

    def ucfirst(self, s):
        return sb.trampoline("language_ucfirst", self.code, s)

    def caseFold(self, s):
        return sb.trampoline("language_caseFold", self.code, s)

    def formatNum(self, n):
        return sb.trampoline("language_formatNum", self.code, n)

    def formatDate(self, format, timestamp=None, local=None):
        return sb.trampoline("language_formatDate", self.code, format, timestamp, local)

    def formatDuration(self, seconds, allowedIntervals=None):
        return sb.trampoline("language_formatDuration", self.code, seconds, allowedIntervals)

    def parseFormattedNumber(self, s):
        return sb.trampoline("language_parseFormattedNumber", self.code, s)

    def convertPlural(self, n, *forms):
        return self.plural(n, *forms)

    def plural(self, n, *forms):
        return sb.trampoline("language_plural", self.code, n, forms)

    def convertGrammar(self, word, case):
        return self.grammar(case, word)

    def grammar(self, case, word):
        return sb.trampoline("language_grammar", self.code, case, word)

    def gender(self, what, masculine, feminine, neutral):
        return sb.trampoline("language_gender", self.code, what, masculine, feminine, neutral)

    def getArrow(self, direction):
        return self._data["arrows"][direction]

    def getDir(self):
        return "rtl" if self._data["RTL"] else "ltr"

    def getDirMark(self, opposite):
        return "\u200F" if bool(self._data["RTL"]) ^ bool(opposite) else "\u200E"

    def getDirMarkEntity(self, opposite):
        return "&rlm;" if bool(self._data["RTL"]) ^ bool(opposite) else "&lrm;"

    def getDurationIntervals(self, seconds, allowedIntervals):
        return sb.trampoline("language_getDurationIntervals", self.code, seconds, allowedIntervals)

def fetchLanguageName(code, inLanguage=None):
    return sb.trampoline("language_fetchLanguageName", code, inLanguage)

def fetchLanguageNames(inLanguage=None, include="mw"):
    return sb.trampoline("language_fetchLangaugeNames", inLanguage, include)

def getFallbacksFor(code):
    return sb.trampoline("language_getFallbacksFor", code)

def isKnownLanguageTag(code):
    return sb.trampoline("language_isKnownLanguageTag", code)

def isSupportedLanguage(code):
    return sb.trampoline("language_isSupportedLanguage", code)

def isValidBuiltinCode(code):
    return sb.trampoline("language_isValidBuiltinCode", code)

def isValidCode(code):
    return sb.trampoline("language_isValidCode", code)
