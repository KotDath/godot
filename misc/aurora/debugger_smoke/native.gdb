# Launch with: gdb -nx --batch -x native.gdb --args GODOT --headless
# --audio-driver Dummy --path GODOT_CPP/examples/aurora_dependency_chain/project
# --quit-after 2
# Requires a debug-symbol build of all three fixture libraries.
set pagination off
set confirm off
set print thread-events off
set breakpoint pending on
set startup-with-shell off
set disable-randomization off
set debuginfod enabled off
handle SIGPIPE nostop noprint pass
break aurora_middle_value
run
python
import gdb
from pathlib import Path

def require_source(function, library, source):
    frame = gdb.selected_frame()
    assert frame.name() == function, frame.name()
    owner = gdb.solib_name(frame.pc())
    assert owner and Path(owner).name == library, owner
    location = frame.find_sal()
    assert location.symtab and location.line > 0, "No source location: " + library
    path = Path(location.symtab.fullname())
    assert path.name == source and path.is_file(), "Matching source is unavailable: " + str(path)
    print("AURORA_GDB_SOURCE:", library, str(path), location.line)

require_source("aurora_middle_value", "libaurora_probe_middle.so", "middle.cpp")
gdb.execute("step")
require_source("aurora_leaf_value", "libaurora_probe_leaf.so", "leaf.cpp")
gdb.execute("finish")
assert int(gdb.parse_and_eval("$1")) == 40, "Wrong leaf return value"
gdb.execute("finish")
assert int(gdb.parse_and_eval("$2")) == 42, "Wrong middle return value"
print("AURORA_GDB_CHAIN_RETURN:40:42")
gdb.execute("continue")
assert gdb.selected_inferior().pid == 0, "Inferior has not exited"
assert int(gdb.parse_and_eval("$_exitcode")) == 0, "Inferior failed"
print("AURORA_GDB_NATIVE_PASS")
end
