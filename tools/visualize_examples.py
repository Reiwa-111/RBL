#!/usr/bin/env python3
"""Run and visualize the scientific RBL examples.

RBL remains responsible for all calculations. This script only runs the RBL
programs, parses their PGM/CSV output and turns it into PNG/GIF visualizations.

Usage (from the repository root):
    python tools/visualize_examples.py
    python tools/visualize_examples.py life
    python tools/visualize_examples.py tile --x 640 --y 432
    python tools/visualize_examples.py tile --full
"""
from __future__ import annotations

import argparse
import csv
import io
import subprocess
import sys
from pathlib import Path
from typing import Iterable

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_OUTPUT = ROOT / "build" / "visualizations"

try:
    from PIL import Image, ImageDraw, ImageOps
except ImportError as exc:  # pragma: no cover - depends on local environment
    raise SystemExit(
        "Missing visualization dependency: Pillow. Install it with "
        "'python -m pip install -r requirements-visualize.txt'."
    ) from exc


def require_matplotlib():
    try:
        import matplotlib

        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except ImportError as exc:  # pragma: no cover - depends on local environment
        raise RuntimeError(
            "Matplotlib is required for simulation plots. Install it with "
            "'python -m pip install -r requirements-visualize.txt'."
        ) from exc
    return plt


def report(message: str) -> None:
    print(f"[visualize] {message}")


def display_path(path: Path) -> str:
    """Prefer repository-relative paths, but allow output outside the checkout."""
    resolved = path.resolve()
    try:
        return str(resolved.relative_to(ROOT))
    except ValueError:
        return str(resolved)


def save_source_data(output_dir: Path, filename: str, content: str) -> Path:
    """Keep the exact text stream produced by RBL beside its visualization."""
    data_dir = output_dir / "data"
    data_dir.mkdir(parents=True, exist_ok=True)
    path = data_dir / filename
    with path.open("w", encoding="utf-8", newline="\n") as stream:
        stream.write(content)
    return path


def run_rbl(relative_source: str, stdin_text: str | None = None,
            timeout: int = 180) -> str:
    """Run one RBL source using the repository's normal CLI/backend."""
    source = (ROOT / relative_source).resolve()
    if not source.is_file():
        raise FileNotFoundError(f"RBL example not found: {source}")

    command = [sys.executable, str(ROOT / "rbl.py"), "run", str(source)]
    try:
        process = subprocess.run(
            command,
            cwd=ROOT,
            input=stdin_text,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=timeout,
            check=False,
        )
    except subprocess.TimeoutExpired as exc:
        raise RuntimeError(
            f"RBL timed out after {timeout}s while running {relative_source}. "
            "Increase --timeout to allow more time."
        ) from exc

    if process.returncode != 0:
        details = (process.stderr or process.stdout or "no diagnostic output").strip()
        raise RuntimeError(
            f"RBL failed for {relative_source} (exit {process.returncode}):\n{details}"
        )
    if not process.stdout.strip():
        raise RuntimeError(f"RBL produced no standard output for {relative_source}.")
    return process.stdout


def pgm_tokens(payload: str) -> Iterable[str]:
    """Yield PGM tokens while correctly ignoring PGM comments."""
    index = 0
    length = len(payload)
    while index < length:
        while index < length and payload[index].isspace():
            index += 1
        if index >= length:
            return
        if payload[index] == "#":
            newline = payload.find("\n", index)
            index = length if newline < 0 else newline + 1
            continue
        start = index
        while index < length and not payload[index].isspace() and payload[index] != "#":
            index += 1
        if start != index:
            yield payload[start:index]
        if index < length and payload[index] == "#":
            newline = payload.find("\n", index)
            index = length if newline < 0 else newline + 1


def parse_pgm_p2(payload: str) -> Image.Image:
    """Parse the plain-text PGM (P2) format emitted by the RBL examples."""
    tokens = iter(pgm_tokens(payload))
    try:
        magic = next(tokens)
        if magic != "P2":
            raise ValueError(f"Expected PGM P2, received {magic!r}.")
        width = int(next(tokens))
        height = int(next(tokens))
        maximum = int(next(tokens))
    except StopIteration as exc:
        raise ValueError("Incomplete PGM header.") from exc

    if width <= 0 or height <= 0 or not 1 <= maximum <= 65535:
        raise ValueError(f"Invalid PGM dimensions or maximum value: {width}x{height}, {maximum}.")

    expected = width * height
    pixels = bytearray(expected)
    count = 0
    for token in tokens:
        if count >= expected:
            raise ValueError("PGM contains more pixel values than its dimensions allow.")
        value = int(token)
        if not 0 <= value <= maximum:
            raise ValueError(f"PGM pixel value {value} is outside 0..{maximum}.")
        pixels[count] = (value * 255 + maximum // 2) // maximum
        count += 1

    if count != expected:
        raise ValueError(f"PGM contains {count} pixels, but {expected} were expected.")
    return Image.frombytes("L", (width, height), bytes(pixels))


def save_mandelbrot(pgm: str, output: Path) -> None:
    gray = parse_pgm_p2(pgm)
    save_source_data(output.parent, "mandelbrot_256.pgm", pgm)
    # Keep the set interior dark and give the escape-time gradient some contrast.
    image = ImageOps.colorize(gray, black=(5, 8, 22), white=(255, 213, 142))
    image.save(output)
    report(f"Mandelbrot 256x256 -> {display_path(output)}")


def save_newton(pgm: str, output: Path) -> None:
    gray = parse_pgm_p2(pgm)
    save_source_data(output.parent, "newton_fractal_512.pgm", pgm)
    palette = ((255, 100, 76), (65, 195, 255), (192, 120, 255))
    colored: list[tuple[int, int, int]] = []
    for value in gray.getdata():
        if value < 142:
            root, base, span = 0, 72, 70
        elif value <= 213:
            root, base, span = 1, 142, 70
        else:
            root, base, span = 2, 214, 40
        brightness = max(0.18, min(1.0, (value - base) / span))
        color = palette[root]
        colored.append(tuple(int(channel * brightness) for channel in color))
    image = Image.new("RGB", gray.size)
    image.putdata(colored)
    image.save(output)
    report(f"Newton fractal {gray.width}x{gray.height} (root-coloured) -> {display_path(output)}")


def render_white_noise(output: Path, timeout: int) -> None:
    pgm = run_rbl("examples/generators/white_noise_64.rbl", timeout=timeout)
    image = parse_pgm_p2(pgm)
    save_source_data(output.parent, "white_noise_64.pgm", pgm)
    image.save(output)
    report(f"White noise {image.width}x{image.height} -> {display_path(output)}")


def render_tile(output: Path, x_offset: int, y_offset: int, timeout: int) -> Image.Image:
    pgm = run_rbl(
        "examples/fractals/mandelbrot_4k_tile.rbl",
        stdin_text=f"{x_offset}\n{y_offset}\n",
        timeout=timeout,
    )
    image = parse_pgm_p2(pgm)
    if image.size != (640, 432):
        raise ValueError(f"4K tile returned unexpected dimensions: {image.size}.")
    save_source_data(output.parent, f"mandelbrot_4k_tile_x{x_offset}_y{y_offset}.pgm", pgm)
    image.save(output)
    report(f"4K tile ({x_offset}, {y_offset}) -> {display_path(output)}")
    return image


def render_full_4k(output: Path, timeout: int) -> None:
    width, height = 3840, 2160
    tile_width, tile_height = 640, 432
    tile_columns = width // tile_width
    tile_rows = height // tile_height
    total_tiles = tile_columns * tile_rows
    full = Image.new("L", (width, height))
    tile_count = 0
    for y in range(0, height, tile_height):
        for x in range(0, width, tile_width):
            pgm = run_rbl(
                "examples/fractals/mandelbrot_4k_tile.rbl",
                stdin_text=f"{x}\n{y}\n",
                timeout=timeout,
            )
            tile = parse_pgm_p2(pgm)
            if tile.size != (tile_width, tile_height):
                raise ValueError(f"Tile ({x}, {y}) returned unexpected dimensions: {tile.size}.")
            full.paste(tile, (x, y))
            tile_count += 1
            report(f"Rendered 4K tile {tile_count}/{total_tiles} at ({x}, {y})")

    ImageOps.colorize(full, black=(5, 8, 22), white=(255, 213, 142)).save(output)
    report(f"Full 3840x2160 Mandelbrot assembled from {tile_count} RBL tiles -> {display_path(output)}")


def selected_indices(count: int, maximum: int) -> list[int]:
    if count <= maximum:
        return list(range(count))
    if maximum <= 1:
        return [count - 1]
    indices = [round(i * (count - 1) / (maximum - 1)) for i in range(maximum)]
    return list(dict.fromkeys(indices))


def read_csv_rows(text: str, expected_columns: set[str]) -> list[dict[str, str]]:
    reader = csv.DictReader(io.StringIO(text))
    if reader.fieldnames is None:
        raise ValueError("CSV output is missing its header.")
    missing = expected_columns.difference(reader.fieldnames)
    if missing:
        raise ValueError(f"CSV output is missing required columns: {', '.join(sorted(missing))}.")
    rows = list(reader)
    if not rows:
        raise ValueError("CSV output contains a header but no data rows.")
    return rows


def render_life(output_dir: Path, timeout: int, max_frames: int) -> None:
    text = run_rbl("examples/simulations/conway_life.rbl", timeout=timeout)
    save_source_data(output_dir, "conway_life.csv", text)
    reader = csv.DictReader(io.StringIO(text))
    required = {"generation", "population", "x", "y"}
    if reader.fieldnames is None or not required.issubset(reader.fieldnames):
        raise ValueError("Conway Life CSV has an unexpected header.")

    width = height = 256
    frames: list[tuple[int, int, Image.Image]] = []
    current_generation: int | None = None
    current_population = 0
    pixels = bytearray(width * height)

    def finish_frame() -> None:
        if current_generation is None:
            return
        frame = Image.frombytes("L", (width, height), bytes(pixels))
        frames.append((current_generation, current_population, frame))

    for row in reader:
        generation = int(row["generation"])
        population = int(row["population"])
        if current_generation != generation:
            finish_frame()
            current_generation = generation
            current_population = population
            pixels = bytearray(width * height)
        if row["x"].strip() and row["y"].strip():
            x, y = int(row["x"]), int(row["y"])
            if not (0 <= x < width and 0 <= y < height):
                raise ValueError(f"Conway Life emitted an out-of-bounds cell: ({x}, {y}).")
            pixels[y * width + x] = 255
    finish_frame()

    if not frames:
        raise ValueError("Conway Life emitted no generations.")
    indices = selected_indices(len(frames), max_frames)
    animation_frames: list[Image.Image] = []
    for index in indices:
        generation, population, board = frames[index]
        frame = Image.new("RGB", (width, height + 22), (12, 15, 23))
        frame.paste(board.convert("RGB"), (0, 22))
        ImageDraw.Draw(frame).text(
            (7, 5), f"GEN {generation:04d}   POP {population:5d}", fill=(225, 231, 242)
        )
        animation_frames.append(frame.quantize(colors=32))

    gif_path = output_dir / "conway_life.gif"
    animation_frames[0].save(
        gif_path,
        save_all=True,
        append_images=animation_frames[1:],
        duration=45,
        loop=0,
        optimize=True,
        disposal=2,
    )
    last_generation, last_population, last_board = frames[-1]
    preview = last_board.resize((768, 768), Image.Resampling.NEAREST)
    preview.save(output_dir / "conway_life_final.png")
    report(
        f"Conway Life: {len(frames)} source frames, {len(animation_frames)} GIF frames, "
        f"final population {last_population} at generation {last_generation} -> {display_path(gif_path)}"
    )


def render_pendulum(output_dir: Path, timeout: int, max_frames: int) -> None:
    text = run_rbl("examples/simulations/double_pendulum.rbl", timeout=timeout)
    save_source_data(output_dir, "double_pendulum.csv", text)
    rows = read_csv_rows(
        text,
        {"step", "time_seconds", "theta1_rad", "theta2_rad", "x1", "y1", "x2", "y2"},
    )
    times = [float(row["time_seconds"]) for row in rows]
    theta1 = [float(row["theta1_rad"]) for row in rows]
    theta2 = [float(row["theta2_rad"]) for row in rows]
    x1 = [float(row["x1"]) for row in rows]
    y1 = [float(row["y1"]) for row in rows]
    x2 = [float(row["x2"]) for row in rows]
    y2 = [float(row["y2"]) for row in rows]

    plt = require_matplotlib()
    figure, (path_axis, angle_axis) = plt.subplots(1, 2, figsize=(13, 5))
    path_axis.plot(x1, y1, linewidth=1.0, label="Bob 1")
    path_axis.plot(x2, y2, linewidth=0.8, label="Bob 2", alpha=0.8)
    path_axis.scatter([0], [0], marker="+", s=70, label="Pivot")
    path_axis.set_title("Double pendulum trajectory")
    path_axis.set_xlabel("X")
    path_axis.set_ylabel("Y")
    path_axis.set_aspect("equal", adjustable="datalim")
    path_axis.grid(True, alpha=0.25)
    path_axis.legend()
    angle_axis.plot(times, theta1, label="θ1")
    angle_axis.plot(times, theta2, label="θ2", alpha=0.8)
    angle_axis.set_title("Angular displacement")
    angle_axis.set_xlabel("Time (s)")
    angle_axis.set_ylabel("Angle (rad)")
    angle_axis.grid(True, alpha=0.25)
    angle_axis.legend()
    figure.tight_layout()
    figure.savefig(output_dir / "double_pendulum.png", dpi=150)
    plt.close(figure)

    indices = selected_indices(len(rows), max_frames)
    width, height = 480, 360
    pivot = (width // 2, 180)
    scale = 78.0
    frames: list[Image.Image] = []
    trail: list[tuple[int, int]] = []
    for index in indices:
        image = Image.new("RGB", (width, height), (13, 18, 29))
        draw = ImageDraw.Draw(image)
        draw.text((12, 10), f"DOUBLE PENDULUM  t={times[index]:.2f}s", fill=(220, 229, 244))
        px1 = round(pivot[0] + x1[index] * scale)
        py1 = round(pivot[1] - y1[index] * scale)
        px2 = round(pivot[0] + x2[index] * scale)
        py2 = round(pivot[1] - y2[index] * scale)
        trail.append((px2, py2))
        if len(trail) > 2:
            draw.line(trail, fill=(74, 105, 148), width=2)
        draw.line((pivot, (px1, py1)), fill=(83, 190, 255), width=5)
        draw.line(((px1, py1), (px2, py2)), fill=(255, 166, 76), width=5)
        draw.ellipse((pivot[0] - 5, pivot[1] - 5, pivot[0] + 5, pivot[1] + 5), fill=(235, 240, 250))
        draw.ellipse((px1 - 9, py1 - 9, px1 + 9, py1 + 9), fill=(83, 190, 255), outline=(235, 245, 255))
        draw.ellipse((px2 - 10, py2 - 10, px2 + 10, py2 + 10), fill=(255, 166, 76), outline=(255, 239, 210))
        frames.append(image.quantize(colors=96))

    gif_path = output_dir / "double_pendulum.gif"
    frames[0].save(
        gif_path,
        save_all=True,
        append_images=frames[1:],
        duration=35,
        loop=0,
        optimize=True,
        disposal=2,
    )
    report(
        f"Double pendulum: {len(rows)} samples, {len(frames)} animation frames -> "
        f"{display_path(output_dir / 'double_pendulum.png')} and {display_path(gif_path)}"
    )


def read_gravity(text: str) -> tuple[list[str], dict[str, list[dict[str, float]]]]:
    rows = read_csv_rows(
        text,
        {"step", "time_seconds", "body", "mass", "x", "y", "z", "vx", "vy", "vz"},
    )
    bodies: dict[str, list[dict[str, float]]] = {}
    order: list[str] = []
    for row in rows:
        name = row["body"]
        if name not in bodies:
            bodies[name] = []
            order.append(name)
        bodies[name].append({
            key: float(row[key])
            for key in ("step", "time_seconds", "mass", "x", "y", "z", "vx", "vy", "vz")
        })
    lengths = {len(values) for values in bodies.values()}
    if len(lengths) != 1:
        raise ValueError("Gravity CSV has different sample counts for different bodies.")
    if len(order) < 2:
        raise ValueError("Gravity CSV must contain at least two bodies.")
    return order, bodies


def render_gravity(output_dir: Path, timeout: int, max_frames: int) -> None:
    text = run_rbl("examples/simulations/gravity_orbit_3d.rbl", timeout=timeout)
    save_source_data(output_dir, "gravity_orbit_3d.csv", text)
    names, bodies = read_gravity(text)
    sample_count = len(bodies[names[0]])
    colors = ["#f6c85f", "#4e79a7", "#e15759", "#59a14f", "#b07aa1", "#76b7b2"]

    plt = require_matplotlib()
    figure = plt.figure(figsize=(9, 8))
    axis = figure.add_subplot(111, projection="3d")
    for body_index, name in enumerate(names):
        series = bodies[name]
        axis.plot(
            [row["x"] for row in series],
            [row["y"] for row in series],
            [row["z"] for row in series],
            label=name,
            color=colors[body_index % len(colors)],
            linewidth=1.1 if body_index else 1.8,
        )
        axis.scatter(
            [series[0]["x"]], [series[0]["y"]], [series[0]["z"]],
            color=colors[body_index % len(colors)], s=22 if body_index else 58,
        )
    axis.set_title("Six-body gravity — 3D trajectories (G = 1, arbitrary units)")
    axis.set_xlabel("X")
    axis.set_ylabel("Y")
    axis.set_zlabel("Z")
    axis.legend(loc="upper left", fontsize=8)
    axis.set_box_aspect((1, 1, 0.65))
    figure.tight_layout()
    figure.savefig(output_dir / "gravity_orbit_3d.png", dpi=160)
    plt.close(figure)

    indices = selected_indices(sample_count, max_frames)
    width, height = 640, 360
    panel_width = width // 2
    center_y = height // 2
    scale = 12.0
    frames: list[Image.Image] = []
    for index in indices:
        image = Image.new("RGB", (width, height), (12, 16, 25))
        draw = ImageDraw.Draw(image)
        draw.text((12, 10), "XY PROJECTION", fill=(220, 229, 244))
        draw.text((panel_width + 12, 10), "XZ PROJECTION", fill=(220, 229, 244))
        for panel_x in (0, panel_width):
            draw.rectangle((panel_x + 8, 34, panel_x + panel_width - 8, height - 12), outline=(56, 66, 84))
            mid_x = panel_x + panel_width // 2
            draw.line((mid_x, 38, mid_x, height - 16), fill=(40, 50, 66), width=1)
            draw.line((panel_x + 12, center_y, panel_x + panel_width - 12, center_y), fill=(40, 50, 66), width=1)

        for body_index, name in enumerate(names):
            series = bodies[name]
            color = tuple(int(colors[body_index % len(colors)].lstrip("#")[i:i + 2], 16) for i in (0, 2, 4))
            xy_points: list[tuple[int, int]] = []
            xz_points: list[tuple[int, int]] = []
            for row in series[:index + 1]:
                xy_points.append((
                    round(panel_width // 2 + row["x"] / scale * (panel_width // 2 - 24)),
                    round(center_y - row["y"] / scale * (height // 2 - 46)),
                ))
                xz_points.append((
                    round(panel_width + panel_width // 2 + row["x"] / scale * (panel_width // 2 - 24)),
                    round(center_y - row["z"] / scale * (height // 2 - 46)),
                ))
            if len(xy_points) > 1:
                draw.line(xy_points, fill=color, width=2)
                draw.line(xz_points, fill=color, width=2)
            for point in (xy_points[-1], xz_points[-1]):
                radius = 5 if body_index == 0 else 3
                draw.ellipse((point[0] - radius, point[1] - radius, point[0] + radius, point[1] + radius), fill=color)

        time_value = bodies[names[0]][index]["time_seconds"]
        draw.text((12, height - 25), f"t={time_value:.3f}s", fill=(220, 229, 244))
        frames.append(image.quantize(colors=96))

    gif_path = output_dir / "gravity_orbit_3d.gif"
    frames[0].save(
        gif_path,
        save_all=True,
        append_images=frames[1:],
        duration=45,
        loop=0,
        optimize=True,
        disposal=2,
    )
    report(
        f"Gravity: {len(names)} bodies, {sample_count} samples each, {len(frames)} animation frames -> "
        f"{display_path(output_dir / 'gravity_orbit_3d.png')} and {display_path(gif_path)}"
    )


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Run RBL scientific examples and create PNG/GIF visualizations."
    )
    parser.add_argument(
        "target",
        nargs="?",
        default="all",
        choices=("all", "mandelbrot", "tile", "newton", "noise", "life", "pendulum", "gravity"),
        help="which example to render (default: all)",
    )
    parser.add_argument("--output-dir", type=Path, default=DEFAULT_OUTPUT,
                        help="directory for generated PNG/GIF files")
    parser.add_argument("--timeout", type=int, default=180,
                        help="timeout in seconds for each RBL program run")
    parser.add_argument("--max-frames", type=int, default=200,
                        help="maximum number of frames in generated animations")
    parser.add_argument("--x", type=int, default=0, help="4K tile horizontal pixel offset")
    parser.add_argument("--y", type=int, default=0, help="4K tile vertical pixel offset")
    parser.add_argument("--full", action="store_true",
                        help="for 'tile', render and assemble all 30 tiles into a 4K image")
    parser.add_argument("--full-4k", action="store_true",
                        help="when target is 'all', render all 30 tiles into a 4K image")
    return parser


def main() -> int:
    args = build_parser().parse_args()
    if args.timeout <= 0 or args.max_frames <= 0:
        print("--timeout and --max-frames must be positive integers.", file=sys.stderr)
        return 2

    output_dir = args.output_dir if args.output_dir.is_absolute() else (ROOT / args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)

    try:
        target = args.target
        if target in ("all", "mandelbrot"):
            save_mandelbrot(
                run_rbl("examples/fractals/mandelbrot_256.rbl", timeout=args.timeout),
                output_dir / "mandelbrot_256.png",
            )
        if target == "tile":
            if args.full:
                render_full_4k(output_dir / "mandelbrot_4k_full.png", args.timeout)
            else:
                render_tile(output_dir / f"mandelbrot_4k_tile_x{args.x}_y{args.y}.png", args.x, args.y, args.timeout)
        elif target == "all":
            render_tile(output_dir / "mandelbrot_4k_tile_x0_y0.png", 0, 0, args.timeout)
            if args.full_4k:
                render_full_4k(output_dir / "mandelbrot_4k_full.png", args.timeout)
        if target in ("all", "newton"):
            save_newton(
                run_rbl("examples/fractals/newton_fractal_512.rbl", timeout=args.timeout),
                output_dir / "newton_fractal_512.png",
            )
        if target in ("all", "noise"):
            render_white_noise(output_dir / "white_noise_64.png", args.timeout)
        if target in ("all", "life"):
            render_life(output_dir, args.timeout, args.max_frames)
        if target in ("all", "pendulum"):
            render_pendulum(output_dir, args.timeout, args.max_frames)
        if target in ("all", "gravity"):
            render_gravity(output_dir, args.timeout, args.max_frames)
    except (OSError, ValueError, RuntimeError, csv.Error) as exc:
        print(f"[visualize] ERROR: {exc}", file=sys.stderr)
        return 1

    report(f"Done. Open the generated files in: {output_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
