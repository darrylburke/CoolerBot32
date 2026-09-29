"""Add -include timegm_compat.h to C++ compiles only.

Putting it in build_flags would also reach assembly (.S) files, where a C
header breaks the assembler. CXXFLAGS is C++-only.
"""
import os
Import("env")

compat = os.path.join(env["PROJECT_DIR"], "src", "compat", "timegm_compat.h")
env.Append(CXXFLAGS=["-include", compat])
