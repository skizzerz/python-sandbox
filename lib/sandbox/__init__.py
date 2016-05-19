import json
import base64
import sys
import errno

__all__ = []

# Sandbox namespaces
NS_SYS = 0 # syscall
NS_SB  = 1 # sandbox
NS_MW  = 2 # mediawiki

# Pipes to the parent process
_pipein = open(3, mode="rt", buffering=1, closefd=False)
_pipeout = open(4, mode="wt", buffering=1, closefd=False)

# Function to send a request to the parent process and get the response back
# We use JSON to serialize values back and forth
def trampoline(name, *args, ns=NS_MW):
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
    if obj["code"] == -1 and obj.get("errno", 0) != 0:
        raise OSError(obj["errno"], data)
    return data

def complete_init():
    trampoline("completeInit", ns=NS_SB)
