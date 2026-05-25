import subprocess
import sys
from pathlib import Path

DXC   = Path("Shaders/Build/dxc.exe")
BIN2C = Path("Shaders/Build/bin2c.exe")

SHADER_DIR = Path("Shaders")
SPV_DIR    = SHADER_DIR / "spv"

GRAPHICS_SHADERS = [
    ("Surface"         , "vert", "frag"),
    ("LineDebug"       , "vert", "frag"),
    ("SurfaceDepthOnly", "vert", "frag"),
    ("UI/Slug"         , "vert", "frag"),
    ("UI/UIShape"      , "vert", "frag"),
    ("UI/UIImage"      , "vert", "frag"),
    ("Skinned"         , "vert", "frag"),
    ("SkinnedDepthOnly", "vert", "frag")
]

COMPUTE_SHADERS = [
    ("TexturePageCopyRGBA"                 , "main"),
    ("TexturePageCopyRG"                   , "main"),
    ("DeferredLighting"                    , "main"),
    ("ExtractNormalCompute"                , "main"),
    ("Shadow/SDSMDepthBoundsInitial"       , "main"),
    ("Shadow/SDSMDepthBoundsReduce"        , "main"),
    ("Shadow/SDSMSetupShadows"             , "main"),
    ("Animation/AnimationCompute"          , "main"),
    ("Animation/AnimateVertices"           , "main"),
    ("PreProcessing/CullDrawArgsCompute"   , "main"),
    ("PreProcessing/HiZBuildCompute"       , "main"),
    ("PreProcessing/HiZDownscaleCompute"   , "main"),
    ("PostProcessing/TonemapCompute"       , "main"),
    ("PostProcessing/HBAOCompute"          , "main"),
    ("PostProcessing/HBAOBlurCompute"      , "main"),
    ("PostProcessing/MLAAEdgeMaskCompute"  , "main"),
    ("PostProcessing/MLAALineLengthCompute", "main"),
    ("PostProcessing/MLAABlendCompute"     , "main")
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

    if header.exists() and header.stat().st_mtime > hlsl.stat().st_mtime:
        print(f"Skipping {src_name}.hlsl ({entry}) -> Up to date.")
        return

    print(f"Compiling {src_name}.hlsl entry {entry} -> {out_name}.spv...")

    # --- FIX: Ensure target subdirectories exist for both .spv and .spv.h ---
    spv.parent.mkdir(parents=True, exist_ok=True)
    header.parent.mkdir(parents=True, exist_ok=True)

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
    print("Processing Graphics Shaders...")
    for name, vs_entry, ps_entry in GRAPHICS_SHADERS:
        compile_shader(name, f"{name}Vert", vs_entry, "vs_6_6")
        compile_shader(name, f"{name}Frag", ps_entry, "ps_6_6")

    # --- FIX: Changed \P to \n to fix SyntaxWarning and prevent corrupted print output ---
    print("\nProcessing Compute Shaders...")
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

    print("\nSUCCESS: All shaders processed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())