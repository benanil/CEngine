import functools
import os
import platform
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Dict, List, NamedTuple, Optional, Tuple

print = functools.partial(print, flush=True)

SHADER_SCRIPT = Path("Shaders/CompileShaders.py")
PROJECT_EXE = "CEngine"


def program_files_roots() -> List[Path]:
    roots: List[Path] = []

    for var in ("ProgramFiles(x86)", "ProgramFiles"):
        value = os.environ.get(var)

        if value and Path(value) not in roots:
            roots.append(Path(value))

    return roots


def run_cmd(
    args: List[str],
    error_msg: str,
    env: Optional[Dict[str, str]] = None,
    quiet: bool = False,
) -> None:
    print(" ".join(str(a) for a in args))

    result = subprocess.run(
        args,
        env=env,
        capture_output=quiet,
        text=quiet,
        encoding="mbcs" if quiet and platform.system() == "Windows" else None,
        errors="replace" if quiet else None,
    )

    if result.returncode != 0:
        print(error_msg)

        if quiet:
            if result.stdout:
                print("----- stdout -----")
                print(result.stdout)

            if result.stderr:
                print("----- stderr -----")
                print(result.stderr)

        sys.exit(result.returncode)


def find_program(names: List[str]) -> Optional[str]:
    for name in names:
        path = shutil.which(name)

        if path:
            return path

    return None


def find_vswhere() -> Optional[Path]:
    for root in program_files_roots():
        candidate = root / "Microsoft Visual Studio" / "Installer" / "vswhere.exe"

        if candidate.exists():
            return candidate

    return None


def find_vcvarsall() -> Path:
    vswhere = find_vswhere()

    if vswhere:
        result = subprocess.run(
            [
                str(vswhere),
                "-latest",
                "-products", "*",
                "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
                "-property", "installationPath",
            ],
            capture_output=True,
            text=True,
            encoding="mbcs",
            errors="replace",
        )

        install_path = result.stdout.strip()

        if result.returncode == 0 and install_path:
            vcvarsall = (
                Path(install_path)
                / "VC"
                / "Auxiliary"
                / "Build"
                / "vcvarsall.bat"
            )

            if vcvarsall.exists():
                return vcvarsall

    matches: List[Path] = []

    for root in program_files_roots():
        vs_root = root / "Microsoft Visual Studio"

        if vs_root.exists():
            matches.extend(vs_root.glob("**/VC/Auxiliary/Build/vcvarsall.bat"))

    if matches:
        matches.sort(reverse=True)
        return matches[0]

    print("[ERROR] Could not find vcvarsall.bat")
    print("Install Visual Studio or Build Tools with the C++ x64 toolchain.")
    sys.exit(1)


def find_sdk_roots() -> List[Path]:
    roots: List[Path] = []

    if platform.system() == "Windows":
        import winreg

        for view in (winreg.KEY_WOW64_32KEY, winreg.KEY_WOW64_64KEY):
            try:
                with winreg.OpenKey(
                    winreg.HKEY_LOCAL_MACHINE,
                    r"SOFTWARE\Microsoft\Windows Kits\Installed Roots",
                    0,
                    winreg.KEY_READ | view,
                ) as key:
                    value, _ = winreg.QueryValueEx(key, "KitsRoot10")
                    roots.append(Path(value))
            except OSError:
                pass

    for base in program_files_roots():
        roots.append(base / "Windows Kits" / "10")

    unique: List[Path] = []

    for root in roots:
        if root.exists() and root not in unique:
            unique.append(root)

    return unique


def parse_version(name: str) -> Optional[Tuple[int, ...]]:
    try:
        return tuple(int(part) for part in name.split("."))
    except ValueError:
        return None


class Sdk(NamedTuple):
    version: str
    root: Path
    tools: Path


def sdk_missing_files(root: Path, name: str) -> List[Path]:
    required = [
        root / "bin" / name / "x64" / "rc.exe",
        root / "bin" / name / "x64" / "mt.exe",
        root / "Include" / name / "um" / "Windows.h",
        root / "Include" / name / "ucrt" / "stdio.h",
        root / "Include" / name / "shared" / "winapifamily.h",
        root / "Lib" / name / "um" / "x64" / "kernel32.lib",
        root / "Lib" / name / "ucrt" / "x64" / "ucrt.lib",
    ]

    return [p for p in required if not p.exists()]


def find_sdk() -> Optional[Sdk]:
    candidates: List[Tuple[Tuple[int, ...], Sdk]] = []

    for root in find_sdk_roots():
        bin_dir = root / "bin"

        if not bin_dir.exists():
            continue

        for entry in bin_dir.iterdir():
            version = parse_version(entry.name)

            if version is None:
                continue

            missing = sdk_missing_files(root, entry.name)

            if missing:
                print(
                    "Skipping incomplete Windows SDK {} (missing {})".format(
                        entry.name, missing[0]
                    )
                )
                continue

            candidates.append((version, Sdk(entry.name, root, entry / "x64")))

    if not candidates:
        return None

    candidates.sort(key=lambda c: c[0], reverse=True)

    return candidates[0][1]


SDK_ENV_PREFIXES = (
    "VSCMD_",
    "WINDOWSSDK",
    "UCRT",
    "UNIVERSALCRT",
    "EXTENSIONSDK",
)

SDK_ENV_NAMES = ("INCLUDE", "LIB", "LIBPATH", "EXTERNAL_INCLUDE", "EXTERNAL_LIB")


def make_clean_env() -> Dict[str, str]:
    clean: Dict[str, str] = {}

    for key, value in os.environ.items():
        upper = key.upper()

        if upper.startswith(SDK_ENV_PREFIXES) or upper in SDK_ENV_NAMES:
            continue

        clean[key] = value

    clean["VSCMD_SKIP_SENDTELEMETRY"] = "1"

    return clean


def parse_env_dump(text: str) -> Dict[str, str]:
    env: Dict[str, str] = {}

    for line in text.splitlines():
        key, sep, value = line.partition("=")

        if sep and key:
            env[key.upper()] = value

    return env


def run_vcvarsall(
    vcvarsall: Path, vcvars_args: str, clean_env: Dict[str, str]
) -> Tuple[Optional[Dict[str, str]], str]:
    temp_bat = Path("_msvc_env.bat")

    temp_bat.write_text(
        """@echo off
call "{}" {}
if errorlevel 1 exit /b %errorlevel%
set
""".format(vcvarsall, vcvars_args),
        encoding="mbcs",
    )

    try:
        result = subprocess.run(
            ["cmd.exe", "/d", "/c", str(temp_bat)],
            capture_output=True,
            text=True,
            encoding="mbcs",
            errors="replace",
            env=clean_env,
        )
    finally:
        try:
            temp_bat.unlink()
        except FileNotFoundError:
            pass

    if result.returncode != 0:
        return None, result.stdout + result.stderr

    return parse_env_dump(result.stdout), ""


def missing_tools(env: Dict[str, str]) -> List[str]:
    path = env.get("PATH", "")

    return [t for t in ("cl", "link", "rc", "mt") if not shutil.which(t, path=path)]


def libs_usable(env: Dict[str, str]) -> bool:
    for entry in env.get("LIB", "").split(";"):
        if entry and (Path(entry) / "kernel32.lib").exists():
            return True

    return False


def env_usable(env: Dict[str, str]) -> bool:
    return not missing_tools(env) and libs_usable(env)


def prepend_paths(env: Dict[str, str], key: str, paths: List[Path]) -> None:
    existing = [e for e in env.get(key, "").split(";") if e]
    added = [str(p) for p in paths if p.exists()]

    env[key] = ";".join(added + existing)


def inject_sdk(env: Dict[str, str], sdk: Sdk) -> None:
    include = sdk.root / "Include" / sdk.version

    prepend_paths(
        env,
        "INCLUDE",
        [include / n for n in ("ucrt", "um", "shared", "winrt", "cppwinrt")],
    )

    lib = sdk.root / "Lib" / sdk.version

    prepend_paths(env, "LIB", [lib / "ucrt" / "x64", lib / "um" / "x64"])
    prepend_paths(env, "PATH", [sdk.tools])

    env["WINDOWSSDKDIR"] = str(sdk.root) + "\\"
    env["WINDOWSSDKVERSION"] = sdk.version + "\\"


def get_msvc_env() -> Dict[str, str]:
    vcvarsall = find_vcvarsall()
    sdk = find_sdk()
    clean_env = make_clean_env()

    print("Using MSVC env: {}".format(vcvarsall))

    if sdk:
        print("Using Windows SDK: {} ({})".format(sdk.version, sdk.root))
    else:
        print("[WARNING] No complete Windows SDK found")

    attempts: List[str] = ["x64"]

    if sdk:
        attempts.append("x64 {}".format(sdk.version))

    failures: List[str] = []
    base: Optional[Dict[str, str]] = None

    for args in attempts:
        print("Running vcvarsall {}".format(args))

        env, output = run_vcvarsall(vcvarsall, args, clean_env)

        if env is None:
            failures.append("vcvarsall {}\n{}".format(args, output))
            continue

        if base is None:
            base = dict(env)

        if env_usable(env):
            return env

        failures.append(
            "vcvarsall {}\nmissing tools: {}\nkernel32.lib on LIB: {}".format(
                args, ", ".join(missing_tools(env)) or "none", libs_usable(env)
            )
        )

    if base is not None and sdk:
        print("vcvarsall did not provide the SDK, injecting it manually")

        inject_sdk(base, sdk)

        if env_usable(base):
            return base

        failures.append(
            "manual injection\nmissing tools: {}\nkernel32.lib on LIB: {}".format(
                ", ".join(missing_tools(base)) or "none", libs_usable(base)
            )
        )

    print("[ERROR] Failed to initialize MSVC environment.")

    for failure in failures:
        print("----- attempt -----")
        print(failure)

    sys.exit(1)


def choose_generator() -> str:
    if shutil.which("ninja"):
        return "Ninja"

    if platform.system() == "Windows":
        print("[ERROR] Ninja not found.")
        print("Install Ninja or change this script to use a Visual Studio CMake generator.")
        sys.exit(1)

    return "Unix Makefiles"


def choose_unix_compiler_env() -> Dict[str, str]:
    env = os.environ.copy()

    cc = find_program(["clang", "gcc", "cc"])
    cxx = find_program(["clang++", "g++", "c++"])

    if not cc or not cxx:
        print("[ERROR] Could not find C/C++ compiler.")
        print("Install clang/gcc on Linux or Xcode Command Line Tools on macOS.")
        sys.exit(1)

    env["CC"] = cc
    env["CXX"] = cxx

    print("Using C compiler:   {}".format(cc))
    print("Using C++ compiler: {}".format(cxx))

    return env


def get_clang_env() -> Dict[str, str]:
    env = get_msvc_env()

    clang = shutil.which("clang-cl", path=env.get("PATH"))

    if not clang:
        print("[ERROR] Could not find clang-cl.")
        sys.exit(1)

    env["CC"] = clang
    env["CXX"] = clang

    print("Using C compiler:   {}".format(clang))
    print("Using C++ compiler: {}".format(clang))

    return env


def compile_shaders() -> None:
    print("Compiling shaders...")

    run_cmd(
        [sys.executable, str(SHADER_SCRIPT)],
        "[ERROR] Failed to compile shaders",
    )


def reset_failed_configure(build_dir: Path) -> None:
    cache = build_dir / "CMakeCache.txt"
    ninja_file = build_dir / "build.ninja"
    makefile = build_dir / "Makefile"

    if cache.exists() and not (ninja_file.exists() or makefile.exists()):
        print("Removing stale CMake configuration: {}".format(build_dir))
        shutil.rmtree(build_dir)


def configure_and_build(config: str, env: Dict[str, str]) -> None:
    build_dir = Path(config)
    generator = choose_generator()

    print("Using CMake generator: {}".format(generator))
    print("Compiling project [{}]...".format(config))

    reset_failed_configure(build_dir)
    build_dir.mkdir(exist_ok=True)

    run_cmd(
        [
            "cmake",
            "-S", ".",
            "-B", str(build_dir),
            "-G", generator,
            "--log-level=WARNING",
            "-DCMAKE_BUILD_TYPE={}".format(config),
        ],
        "[ERROR] CMake configure failed for {}".format(config),
        env=env,
        quiet=True,
    )

    run_cmd(
        [
            "cmake",
            "--build", str(build_dir),
            "--config", config,
        ],
        "[ERROR] Build failed for {}".format(config),
        env=env,
    )


def get_exe_path(config: str) -> Path:
    if platform.system() == "Windows":
        return Path(config) / config / "{}.exe".format(PROJECT_EXE)

    return Path(config) / PROJECT_EXE


def delete_old_exe(config: str) -> None:
    exe = get_exe_path(config)

    if exe.exists():
        print("Deleting old executable: {}".format(exe))
        exe.unlink()


def get_config() -> str:
    if len(sys.argv) <= 1:
        return "Debug"

    arg = sys.argv[1].lower()

    if arg == "debug":
        return "Debug"

    if arg == "release":
        return "Release"

    print("[ERROR] Unknown build config: {}".format(sys.argv[1]))
    print("Usage: python Compile.py [Debug|Release]")
    sys.exit(1)


def run_exe(config: str) -> None:
    exe = get_exe_path(config)

    if not exe.exists():
        print("[ERROR] Missing executable: {}".format(exe))
        sys.exit(1)

    print("Running {}...".format(exe))

    if platform.system() == "Windows":
        os.startfile(str(exe))
    else:
        subprocess.Popen([str(exe)])


def main() -> int:
    config = get_config()

    compile_shaders()
    delete_old_exe(config)

    if platform.system() == "Windows":
        env = get_msvc_env()
    else:
        env = choose_unix_compiler_env()

    configure_and_build(config, env)
    run_exe(config)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())