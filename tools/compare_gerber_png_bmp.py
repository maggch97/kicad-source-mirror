from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable

from PIL import Image, ImageOps

Image.MAX_IMAGE_PIXELS = None


DEFAULT_MODES = (
    {"name": "dpi120", "dpi": "120", "tile_sizes": ("auto", "257x193", "641x257")},
    {"name": "dpi180x90", "dpi": "180x90", "tile_sizes": ("auto", "193x129", "577x211")},
)

IGNORED_STDERR_SNIPPETS = (
    "Can't create registry key 'HKCU\\Software\\kicad-cli'",
    "Can't open registry key 'HKCU\\Software\\kicad-cli'",
)


@dataclass
class RenderResult:
    command: list[str]
    returncode: int
    stdout: str
    stderr: str
    duration_sec: float


def repo_root() -> Path:
    return Path(__file__).resolve().parent.parent


def default_cli_path() -> Path:
    return repo_root() / "build" / "install" / "msvc-win64-release" / "bin" / "kicad-cli.exe"


def default_vcpkg_bin() -> Path:
    return repo_root() / "build" / "msvc-win64-release" / "vcpkg_installed" / "x64-windows" / "bin"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Compare gerber PNG output against tiled BMP output.")
    parser.add_argument("--exe", type=Path, default=default_cli_path(), help="Path to kicad-cli.exe")
    parser.add_argument("--vcpkg-bin", type=Path, default=default_vcpkg_bin(),
                        help="Path to the vcpkg runtime DLL directory")
    parser.add_argument("--test-root", type=Path, default=repo_root() / "test_cases",
                        help="Directory containing per-case subdirectories with layer.gbr")
    parser.add_argument("--output-root", type=Path, default=repo_root() / "out" / "gerber_png_bmp_compare",
                        help="Directory for generated images and reports")
    parser.add_argument("--case", action="append", default=[],
                        help="Case directory name to run. Can be passed multiple times.")
    parser.add_argument("--mode", action="append", default=[],
                        help="Mode name to run. Can be passed multiple times.")
    parser.add_argument("--tile-size", action="append", default=[],
                        help="Override tile sizes to test. Can be passed multiple times.")
    parser.add_argument("--keep-going", action="store_true",
                        help="Continue running after a failing render or pixel mismatch")
    return parser.parse_args()


def list_cases(test_root: Path) -> list[Path]:
    cases: list[Path] = []

    for child in sorted(test_root.iterdir()):
        layer = child / "layer.gbr"

        if child.is_dir() and layer.exists():
            cases.append(layer)

    if not cases:
        raise RuntimeError(f"No test cases found under {test_root}")

    return cases


def selected_modes(args: argparse.Namespace) -> list[dict]:
    modes = [dict(mode) for mode in DEFAULT_MODES]

    if args.mode:
        wanted = set(args.mode)
        modes = [mode for mode in modes if mode["name"] in wanted]

    if args.tile_size:
        for mode in modes:
            mode["tile_sizes"] = tuple(args.tile_size)

    if not modes:
        raise RuntimeError("No test modes selected")

    return modes


def build_env(exe_path: Path, vcpkg_bin: Path, output_root: Path) -> dict[str, str]:
    env = os.environ.copy()
    env["PATH"] = os.pathsep.join(
        [
            str(exe_path.parent),
            str(vcpkg_bin),
            env.get("PATH", ""),
        ]
    )
    env["KICAD_STOCK_DATA_HOME"] = str(exe_path.parent.parent / "share" / "kicad")
    env["KICAD_CONFIG_HOME"] = str(output_root / "_kicad_config")
    return env


def filtered_stderr(stderr: str) -> str:
    kept: list[str] = []

    for line in stderr.splitlines():
        if any(snippet in line for snippet in IGNORED_STDERR_SNIPPETS):
            continue

        if line.strip():
            kept.append(line)

    return "\n".join(kept)


def run_command(command: list[str], env: dict[str, str]) -> RenderResult:
    started = time.perf_counter()
    completed = subprocess.run(command, capture_output=True, text=True, env=env)
    ended = time.perf_counter()
    return RenderResult(
        command=command,
        returncode=completed.returncode,
        stdout=completed.stdout,
        stderr=completed.stderr,
        duration_sec=ended - started,
    )


def image_to_bits(image: Image.Image) -> bytes:
    gray = image.convert("L")
    return bytes(1 if value < 128 else 0 for value in gray.tobytes())


def write_diff_image(reference: Image.Image, candidate: Image.Image, output_path: Path) -> None:
    if reference.size != candidate.size:
        return

    ref_rgb = reference.convert("RGB")
    cand_rgb = candidate.convert("RGB")
    diff = Image.new("RGB", ref_rgb.size, (255, 255, 255))

    ref_bits = image_to_bits(ref_rgb)
    cand_bits = image_to_bits(cand_rgb)

    pixels = diff.load()
    width, height = diff.size

    for index, (ref_bit, cand_bit) in enumerate(zip(ref_bits, cand_bits)):
        if ref_bit == cand_bit:
            continue

        x = index % width
        y = index // width
        pixels[x, y] = (255, 0, 0)

    diff.save(output_path)


def render_png(exe_path: Path, input_path: Path, output_path: Path, dpi: str,
               env: dict[str, str]) -> RenderResult:
    command = [
        str(exe_path),
        "gerber",
        "convert",
        "png",
        str(input_path),
        "-o",
        str(output_path),
        "--dpi",
        dpi,
        "--no-antialias",
        "--foreground",
        "#000000",
        "--background",
        "#FFFFFF",
    ]

    return run_command(command, env)


def render_bmp(exe_path: Path, input_path: Path, output_path: Path, dpi: str, tile_size: str,
               env: dict[str, str]) -> RenderResult:
    command = [
        str(exe_path),
        "gerber",
        "convert",
        "bmp",
        str(input_path),
        "-o",
        str(output_path),
        "--dpi",
        dpi,
        "--tile-size",
        tile_size,
    ]

    return run_command(command, env)


def ensure_success(result: RenderResult, keep_going: bool) -> None:
    extra_stderr = filtered_stderr(result.stderr)

    if result.returncode == 0 and not extra_stderr:
        return

    message = [
        f"Command failed: {' '.join(result.command)}",
        f"Exit code: {result.returncode}",
    ]

    if result.stdout.strip():
        message.append("stdout:")
        message.append(result.stdout.strip())

    if result.stderr.strip():
        message.append("stderr:")
        message.append(result.stderr.strip())

    text = "\n".join(message)

    if keep_going:
        print(text, file=sys.stderr)
        return

    raise RuntimeError(text)


def make_report(results: Iterable[dict], report_path: Path) -> None:
    total = 0
    passed = 0
    failed = 0

    lines = [
        "# Gerber PNG vs BMP Tile Compare Report",
        "",
        "## Summary",
        "",
    ]

    for result in results:
        total += 1

        if result["match"]:
            passed += 1
        else:
            failed += 1

    lines.extend(
        [
            f"- Total comparisons: {total}",
            f"- Passed: {passed}",
            f"- Failed: {failed}",
            "",
            "## Notes",
            "",
            "- PNG is used as the reference image.",
            "- Before comparing pixels, the PNG is vertically flipped to match the BMP workaround path.",
            "- PNG is rendered with white background and no antialiasing so it matches BMP conditions as closely as possible.",
            "",
            "## Results",
            "",
            "| Case | Mode | DPI | Tile Size | PNG Size | BMP Size | Match | Diff Pixels |",
            "| --- | --- | --- | --- | --- | --- | --- | ---: |",
        ]
    )

    for result in results:
        lines.append(
            f"| {result['case']} | {result['mode']} | {result['dpi']} | {result['tile_size']} | "
            f"{result['png_size'][0]}x{result['png_size'][1]} | {result['bmp_size'][0]}x{result['bmp_size'][1]} | "
            f"{'yes' if result['match'] else 'no'} | {result['diff_pixels']} |"
        )

    report_path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    args = parse_args()
    exe_path = args.exe.resolve()
    vcpkg_bin = args.vcpkg_bin.resolve()
    test_root = args.test_root.resolve()
    output_root = args.output_root.resolve()

    if output_root.exists():
        shutil.rmtree(output_root)

    output_root.mkdir(parents=True, exist_ok=True)
    env = build_env(exe_path, vcpkg_bin, output_root)
    cases = list_cases(test_root)
    modes = selected_modes(args)
    results: list[dict] = []
    any_failures = False

    if args.case:
        wanted_cases = set(args.case)
        cases = [case for case in cases if case.parent.name in wanted_cases]

    if not cases:
        raise RuntimeError("No test cases selected")

    for layer_path in cases:
        case_name = layer_path.parent.name

        for mode in modes:
            mode_name = mode["name"]
            dpi = mode["dpi"]
            mode_root = output_root / case_name / mode_name
            mode_root.mkdir(parents=True, exist_ok=True)
            png_path = mode_root / "reference.png"

            png_result = render_png(exe_path, layer_path, png_path, dpi, env)
            ensure_success(png_result, args.keep_going)

            if png_result.returncode != 0:
                any_failures = True
                continue

            with Image.open(png_path) as png_image:
                png_reference = ImageOps.flip(png_image.convert("RGB"))

            png_bits = image_to_bits(png_reference)

            for tile_size in mode["tile_sizes"]:
                bmp_path = mode_root / f"tile_{tile_size.replace('x', '_')}.bmp"
                bmp_result = render_bmp(exe_path, layer_path, bmp_path, dpi, tile_size, env)
                ensure_success(bmp_result, args.keep_going)

                if bmp_result.returncode != 0:
                    any_failures = True
                    continue

                with Image.open(bmp_path) as bmp_source:
                    bmp_image = bmp_source.convert("RGB")

                bmp_bits = image_to_bits(bmp_image)
                match = png_reference.size == bmp_image.size and png_bits == bmp_bits
                diff_pixels = 0
                diff_path = None

                if not match:
                    any_failures = True
                    if png_reference.size == bmp_image.size:
                        diff_pixels = sum(1 for left, right in zip(png_bits, bmp_bits) if left != right)
                    else:
                        diff_pixels = max(len(png_bits), len(bmp_bits))
                    diff_path = mode_root / f"diff_{tile_size.replace('x', '_')}.png"
                    write_diff_image(png_reference, bmp_image, diff_path)

                results.append(
                    {
                        "case": case_name,
                        "mode": mode_name,
                        "dpi": dpi,
                        "tile_size": tile_size,
                        "png_size": png_reference.size,
                        "bmp_size": bmp_image.size,
                        "match": match,
                        "diff_pixels": diff_pixels,
                        "png_stdout": png_result.stdout.strip(),
                        "bmp_stdout": bmp_result.stdout.strip(),
                        "png_stderr": filtered_stderr(png_result.stderr),
                        "bmp_stderr": filtered_stderr(bmp_result.stderr),
                        "png_duration_sec": round(png_result.duration_sec, 3),
                        "bmp_duration_sec": round(bmp_result.duration_sec, 3),
                        "diff_path": str(diff_path) if diff_path else "",
                    }
                )

    json_report = output_root / "report.json"
    md_report = output_root / "report.md"
    json_report.write_text(json.dumps(results, indent=2), encoding="utf-8")
    make_report(results, md_report)

    print(f"Wrote JSON report to: {json_report}")
    print(f"Wrote Markdown report to: {md_report}")

    return 1 if any_failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
