# CompileShaders.py
import subprocess
import sys
from pathlib import Path

DXC   = Path("Shaders/Build/dxc.exe")
BIN2C = Path("Shaders/Build/bin2c.exe")

SHADER_DIR = Path("Shaders")
SPV_DIR    = SHADER_DIR / "spv"

GRAPHICS_SHADERS = [
    ("Surface"  , "vert", "frag"),
    ("Skinned"  , "vert", "frag"),
    ("LineDebug", "vert", "frag"),
    ("SurfaceDepthOnly", "vert", "frag"),
    ("SkinnedDepthOnly", "vert", "frag")
]

COMPUTE_SHADERS = [
    ("AnimationCompute"       , "main"),
    ("AnimateVertices"        , "main"),
    ("CullDrawArgsCompute"    , "main"),
    ("TonemapCompute"         , "main"),
    ("HiZBuildCompute"        , "main"),
    ("HiZDownscaleCompute"    , "main"),
    ("HBAONormalCompute"      , "main"),
    ("HBAOCompute"            , "main"),
    ("HBAOBlurCompute"        , "main"),
    ("DepthResolveMSAACompute", "main")
]

def run_cmd(args: list[str], error_msg: str):
    result = subprocess.run(args)
    if result.returncode != 0:
        print(error_msg)
        sys.exit(result.returncode)


def compile_shader(src_name: str, out_name: str, entry: str, target: str):
    hlsl   = SHADER_DIR / f"{src_name}.hlsl"
    spv    = SHADER_DIR / f"{out_name}.spv"
    header = SPV_DIR    / f"{out_name}.spv.h"
    require_file(hlsl)
    print(f"Compiling {src_name}.hlsl entry {entry} -> {out_name}.spv...")

    run_cmd(
        [
            str(DXC),
            "-spirv",
            "-fspv-target-env=vulkan1.1",
            "-T", target,
            "-E", entry,
            "-enable-16bit-types",
            str(hlsl),
            "-Fo", str(spv),
        ],
        f"[ERROR] Failed to compile {src_name}.hlsl entry {entry}",
    )

    run_cmd(
        [str(BIN2C), "-o", str(header), str(spv)],
        f"[ERROR] bin2c failed to process {out_name}.spv"
    )
    spv.unlink()


def compile_all_shaders():
    print("Compiling Graphics Shaders...")

    for name, vs_entry, ps_entry in GRAPHICS_SHADERS:
        compile_shader(name, f"{name}Vert", vs_entry, "vs_6_6")
        compile_shader(name, f"{name}Frag", ps_entry, "ps_6_6")

    print("Compiling Compute Shaders...")
    for name, entry in COMPUTE_SHADERS:
        compile_shader(name, name, entry, "cs_6_6")


def require_file(path: Path):
    if not path.exists():
        print(f"[ERROR] Missing file: {path}")
        sys.exit(1)


def main() -> int:
    SPV_DIR.mkdir(parents=True, exist_ok=True)
    require_file(DXC)
    require_file(BIN2C)
    compile_all_shaders()

    print("SUCCESS: All shaders compiled and headers generated.\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
