# Build.py
import os
import platform
import shutil
import subprocess
import sys
from pathlib import Path

SHADER_SCRIPT = Path("Shaders/CompileShaders.py")
PROJECT_EXE   = "CPlayground"

VSWHERE = Path(
    r"C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
)


def run_cmd(args: list[str], error_msg: str, env=None):
    print(" ".join(str(a) for a in args))

    result = subprocess.run(args, env=env)

    if result.returncode != 0:
        print(error_msg)
        sys.exit(result.returncode)


def find_program(names: list[str]) -> str | None:
    for name in names:
        path = shutil.which(name)
        if path:
            return path

    return None


def find_vcvarsall() -> Path:
    if VSWHERE.exists():
        result = subprocess.run(
            [
                str(VSWHERE),
                "-latest",
                "-products", "*",
                "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
                "-property", "installationPath",
            ],
            capture_output=True,
            text=True,
        )

        install_path = result.stdout.strip()

        if result.returncode == 0 and install_path:
            vcvarsall = (Path(install_path) / "VC" / "Auxiliary" / "Build" / "vcvarsall.bat")

            if vcvarsall.exists():
                return vcvarsall

    roots = [
        Path(r"C:\Program Files\Microsoft Visual Studio"),
        Path(r"C:\Program Files (x86)\Microsoft Visual Studio"),
    ]

    matches: list[Path] = []

    for root in roots:
        if root.exists():
            matches.extend(root.glob(r"**/VC/Auxiliary/Build/vcvarsall.bat"))

    if matches:
        matches.sort(reverse=True)
        return matches[0]

    print("[ERROR] Could not find vcvarsall.bat")
    print("Install Visual Studio or Build Tools with the C++ x64 toolchain.")
    sys.exit(1)


def get_msvc_env() -> dict[str, str]:
    vcvarsall = find_vcvarsall()

    print(f"Using MSVC env: {vcvarsall}")

    command = f'call "{vcvarsall}" x64 && set'

    result = subprocess.run(
        ["cmd.exe", "/s", "/c", command],
        capture_output=True,
        text=True,
    )

    if result.returncode != 0:
        print("[ERROR] Failed to initialize MSVC environment.")
        sys.exit(result.returncode)

    env = os.environ.copy()

    for line in result.stdout.splitlines():
        if "=" in line:
            key, value = line.split("=", 1)
            env[key] = value

    return env


def choose_generator() -> str:
    if shutil.which("ninja"):
        return "Ninja"

    return "Unix Makefiles"


def choose_unix_compiler_env() -> dict[str, str]:
    env = os.environ.copy()

    cc = find_program(["clang", "gcc", "cc"])
    cxx = find_program(["clang++", "g++", "c++"])

    if not cc or not cxx:
        print("[ERROR] Could not find C/C++ compiler.")
        print("Install clang/gcc on Linux or Xcode Command Line Tools on macOS.")
        sys.exit(1)

    env["CC"] = cc
    env["CXX"] = cxx

    print(f"Using C compiler:   {cc}")
    print(f"Using C++ compiler: {cxx}")
    return env


def compile_shaders():
    print("Compiling shaders...")
    run_cmd(
        [sys.executable, str(SHADER_SCRIPT)],
        "[ERROR] Failed to compile shaders",
    )


def configure_and_build(config: str, env: dict[str, str]):
    build_dir = Path(config)
    generator = choose_generator()

    print(f"Using CMake generator: {generator}")
    print(f"Compiling project [{config}]...")

    build_dir.mkdir(exist_ok=True)

    run_cmd(
        [
            "cmake",
            "-S", ".",
            "-B", str(build_dir),
            "-G", generator,
            f"-DCMAKE_BUILD_TYPE={config}",
        ],
        f"[ERROR] CMake configure failed for {config}",
        env=env,
    )

    run_cmd(
        [ "cmake", "--build", str(build_dir) ],
        f"[ERROR] Build failed for {config}", 
        env=env
    )


def get_exe_path(config: str) -> Path:
    system = platform.system()

    if system == "Windows":
        return Path(config) / config / f"{PROJECT_EXE}.exe"

    return Path(config) / PROJECT_EXE


def run_exe(config: str):
    exe = get_exe_path(config)

    if not exe.exists():
        print(f"[ERROR] Missing executable: {exe}")
        sys.exit(1)

    print(f"Running {exe}...")

    if platform.system() == "Windows":
        os.startfile(exe)
    else:
        subprocess.Popen([str(exe)])


def get_config() -> str:
    if len(sys.argv) <= 1:
        return "Debug"

    arg = sys.argv[1].lower()

    if arg == "debug":
        return "Debug"

    if arg == "release":
        return "Release"

    print(f"[ERROR] Unknown build config: {sys.argv[1]}")
    print("Usage: python Build.py [Debug|Release]")
    sys.exit(1)


def delete_old_exe(config: str):
    exe = get_exe_path(config)

    if exe.exists():
        print(f"Deleting old executable: {exe}")
        exe.unlink()

def main() -> int:
    config = get_config()

    delete_old_exe(config)

    compile_shaders()

    if platform.system() == "Windows":
        env = get_msvc_env()
    else:
        env = choose_unix_compiler_env()

    configure_and_build(config, env)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())