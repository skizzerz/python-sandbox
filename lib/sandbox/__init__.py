import json
import base64
import sys
import errno

__all__ = []

# Sandbox namespaces
NS_SYS = 0 # syscall
NS_SB  = 1 # sandbox
NS_APP = 2 # application

try:
    StopAsyncIteration
except:
    StopAsyncIteration = StopIteration

# Error mapping
EXCEPTION_MAP = {
    1: ImportError,
    2: IndexError,
    3: KeyError,
    4: MemoryError,
    5: NotImplementedError,
    6: OSError,
    7: OverflowError,
    8: RuntimeError,
    9: StopIteration,
    10: StopAsyncIteration,
    11: SyntaxError,
    12: TypeError,
    13: ValueError,
    14: ZeroDivisionError
}

# Pipes to the parent process
_pipein = open(3, mode="rt", buffering=1, closefd=False)
_pipeout = open(4, mode="wt", buffering=1, closefd=False)

# Function to send a request to the parent process and get the response back
# We use JSON to serialize values back and forth
def trampoline(name, *args, ns=NS_APP):
    obj = {"ns": ns, "name": name, "args": list(args)}
    serialized = json.dumps(obj, separators=(",", ":"))
    _pipeout.write(serialized + "\n")
    response = _pipein.readline()
    if response == "":
        sys.exit(-errno.EIO)
    obj = json.loads(response)
    data = obj.get("data")
    if obj.get("base64", False) is True:
        data = base64.b64decode(data, validate=True)
    if ns == NS_SYS:
        if obj["code"] == -1 and obj.get("errno", 0) != 0:
            raise OSError(obj["errno"], data)
    elif obj["code"] < 0:
        sys.exit(obj["code"])
    elif obj["code"] > 0:
        exc = EXCEPTION_MAP.get(obj["code"], RuntimeError)
        if exc is ImportError and isinstance(data, dict):
            raise ImportError(data["message"], name=data["name"], path=data["path"])
        elif exc is OSError:
            if isinstance(data, dict):
                strerror = data.get("strerror") or os.strerror(obj["errno"])
                raise OSError(obj["errno"], strerror,
                              filename=data["filename"],
                              filename2=data.get("filename2"))
            else:
                raise OSError(obj["errno"], data)
        elif exc is SyntaxError:
            raise SyntaxError(data["message"],
                              filename=data["filename"],
                              lineno=data["lineno"],
                              offset=data["offset"],
                              text=data["text"])
        else:
            raise exc(data)

    return data

def complete_init():
    trampoline("complete_init", ns=NS_SB)
