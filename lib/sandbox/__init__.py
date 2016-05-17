import json
import base64

__all__ = []

# Pipes to the parent process
_pipein = open(3, mode="rt", buffering=1, closefd=False)
_pipeout = open(4, mode="wt", buffering=1, closefd=False)

# Function to send a request to the parent process and get the response back
# We use JSON to serialize values back and forth
def trampoline(name, *args):
    obj = {"name": name, "args": list(args)}
    serialized = json.dumps(obj, separators=(",", ":"))
    _pipeout.write(serialized + "\n")
    response = _pipein.readline()
    if response == "":
        raise ValueError("Empty response from parent")
    obj = json.loads(response)
    data = obj.get("data")
    if obj.get("base64", False) is True:
        data = base64.b64decode(data, validate=True)
    if obj["code"] == -1 and obj.get("errno", 0) != 0:
        raise OSError(obj["errno"], data)
    return data

def complete_init():
    trampoline("complete_init")
