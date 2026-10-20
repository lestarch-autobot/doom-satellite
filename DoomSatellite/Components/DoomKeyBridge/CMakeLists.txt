# Python (fprime-python) component: the FPP model is autocoded to C++ bindings and a Python base class, and the Python
# implementation is installed into the fprime_py package alongside them.
register_fprime_library(
    AUTOCODER_INPUTS
        "${CMAKE_CURRENT_LIST_DIR}/DoomKeyBridge.fpp"
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/DoomKeyBridge.py"
)
