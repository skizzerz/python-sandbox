# This script is the first thing run in the sandbox environment after
# python itself is initialized. It performs some post-init steps to finish
# setting up the sandbox. The scope here is shared with the user's code,
# e.g. the package for both this and the user code is __main__.

# Import a bunch of mw libraries that are always available
# This saves modules from needing a lot of import statement boilerplate
# and also matches the lua API in autoloading these libraries
import mw
import mw.language
import mw.message
import mw.html
#import mw.site
#import mw.text
#import mw.title
#import mw.uri

# Inform our parent that our init is finished, it can use this to further
# reduce the privileges the sandbox is able to perform if necessary.
def finish():
    import sandbox
    sandbox.complete_init()

finish()
del finish
