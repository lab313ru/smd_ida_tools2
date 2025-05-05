
import sys
import subprocess
import os
import platform

from argparse import ArgumentParser
parser = ArgumentParser()
parser.add_argument("--build-x86", dest="build_x86", default=False, action="store_true", help="Build x86 libs?")
parser.add_argument("--dry-run", dest="dry_run", default=False, action="store_true", help="Don't issue configure command; just print it.")
parser.add_argument("-j", "--processes", default=None, type=int, help="Run make with N processes (defaults to 1)")
parser.add_argument("-v", "--verbose", default=False, action="store_true", help="Run configure in verbose mode")
parser.add_argument("-N", "--dont-build", default=False, action="store_true", help="Don't build; just configure")
parser.add_argument("-d", "--debug-build", default=False, action="store_true", help="Build a debug build")
parser.add_argument("-A", "--asan-build", default=False, action="store_true")
parser.add_argument("--prefix", dest="prefix", type=str, help="Override install path prefix")
clopts = parser.parse_args()

qtver = "5.15.2"

configure_script, build_command, default_install_dir = {
    "win32" : (
        "configure.bat",
        "jom",
        "C:/Qt/",
    ),
    "linux" : (
        "configure",
        "make",
        "/usr/local/Qt/",
    ),
    "darwin" : (
        "configure",
        "make",
        "/Users/Shared/Qt/",
    ),
    }[sys.platform]

install_dir = clopts.prefix if clopts.prefix else default_install_dir

options = {
    "common" : [
        "-nomake", "tests",
        "-nomake", "examples",
        "-qtnamespace", "QT",
        "-confirm-license",
        "-accessibility",
        "-opensource",
        "-force-debug-info",
        "-no-icu",
        "-no-ssl",
        ],
    "win32" : [
        "-platform", "win32-msvc2015",
        "-opengl", "desktop",
        "-qt-freetype",
        "-qt-harfbuzz",
        ],
    "linux" : [
        "-platform", ("linux-g++-32" if clopts.build_x86 else "linux-g++-64"),
        "-fontconfig",
        "-system-freetype",
        "-qt-libpng",
        "-glib",
        "-xcb",
        "-xcb-xlib",
        "-bundled-xcb-xinput",
        "-dbus",
        "-sql-sqlite",
        "-qt-sqlite",
        "-gtk",
        "-qt-harfbuzz",
        ],
    "darwin" : [
        "-platform", (("macx-clang-32" if clopts.build_x86 else ("macx-macos11-clang" if platform.machine() == "arm64" else "macx-clang"))),
        "-debug-and-release",
        "-qt-freetype",
        "-qt-libpng",
        "-sql-sqlite",
        ]
    }

if sys.platform == "win32" and not clopts.build_x86:
    print("WARNING WARNING WARNING WARNING WARNING WARNING WARNING WARNING")
    print("Hopefully you are running this in a Visual Studio x64 prompt!")
    print("WARNING WARNING WARNING WARNING WARNING WARNING WARNING WARNING")

arch_name = "" if clopts.build_x86 else ("-arm64" if platform.machine() == "arm64" else "-x64")
qtsrc_dir = os.path.join(os.getcwd(), "..")
argv = [os.path.join(qtsrc_dir, configure_script)]
argv.extend(options["common"])
argv.extend(options[sys.platform])
argv.extend(["-prefix", (install_dir + "%s%s%s%s") % (
    qtver,
    arch_name,
    "-debug" if clopts.debug_build else "",
    "-asan" if clopts.asan_build else "")])

if sys.platform == "linux":
    if clopts.debug_build:
        argv.append("--debug")
        argv.append("--no-optimize-debug")
        # argv.append("--gdb-index") # Not supported on CentOS7. Damn.
    if clopts.asan_build:
        argv.append("--sanitize")
        argv.append("address")

if clopts.verbose:
    argv.append("--verbose")

def run(argv):
    if clopts.dry_run or clopts.verbose:
        uf_argv = [argv[0]] # user-friendly argv
        for arg in argv[1:]:
            uf_argv.append("\"%s\"" % arg)
        print(" ".join(uf_argv))
    if not clopts.dry_run:
        subprocess.check_call(argv)

def phase(which):
    print("*" * 75)
    print("{:^75}".format(which))
    print("*" * 75)

phase("Configuring")
run(argv)

if not clopts.dont_build:
    phase("Building")
    argv = [build_command]
    if clopts.processes:
        argv.extend(["-j", str(clopts.processes)])
    run(argv)

    phase("Installing")
    argv = []
    if sys.platform != "win32":
        argv.append("sudo")
    argv.append(build_command)
    argv.append("install")
    run(argv)
