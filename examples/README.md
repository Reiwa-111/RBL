# RBL 0.7.2 — example programs

This directory contains 44 RBL programs: twenty beginner-course lessons, eight existing introductory/compatibility examples and sixteen larger algorithmic, numerical, statistical, simulation and image-generation workloads. The simulations and pixel calculations are performed by RBL; Python is optional and is used only by the separate visualization pipeline.

## Start here: Beginner Course

New to programming or RBL? Follow the [20-lesson Beginner Course](beginner/README.md). Choose the [Russian guide](beginner/README_RUS.md) or the [English guide](beginner/README_ENG.md). It introduces the language through small standalone programs before the larger examples below.

## Basic language examples

These compact programs cover the language surface, standard library, input and file I/O. `game_of_life.rbl` is a small dictionary/string/file-I/O stress example; the larger CSV-based simulation is `simulations/conway_life.rbl`.

| Program | What it demonstrates | Output / side effects |
|---|---|---|
| [`blocks_demo.rbl`](blocks_demo.rbl) | Functions, conditionals and inclusive range loops. | Branch result and loop values. |
| [`test.rbl`](test.rbl) | Variables, reassignment, functions, conditionals, loops and logging. | Short language smoke-test output. |
| [`input_demo.rbl`](input_demo.rbl) | Reading one line from standard input. | Greets the supplied name. |
| [`stdlib.rbl`](stdlib.rbl) | Global utility functions and basic file I/O. | Prints utility results; writes and reads `/tmp/rbl_example.txt`. |
| [`stdlib_demo.rbl`](stdlib_demo.rbl) | Namespaced math, string, filesystem, time and random APIs. | Prints representative standard-library results. |
| [`game_of_life.rbl`](game_of_life.rbl) | A compact 48×24 Conway simulation using dictionaries, nested loops and strings. | ASCII snapshots every five generations; writes `life_report.txt` in the working directory. |
| [`mega_test.rbl`](mega_test.rbl) | Broad language smoke test: numeric types, functions, branching, ranges, Unicode and logging. | Sectioned console output. |
| [`mega_test_v07.rbl`](mega_test_v07.rbl) | RBL 0.7-era syntax and standard-library smoke test. | Console output covering core and namespaced APIs. |

## Run an example

Run commands from the repository root. The CLI compiles and executes the selected source file:

```bash
python3 rbl.py check examples/simulations/double_pendulum.rbl
python3 rbl.py run examples/simulations/double_pendulum.rbl
```

To save a data stream, redirect standard output. The programs deliberately emit standard formats rather than custom diagnostic protocols:

```bash
python3 rbl.py run examples/simulations/double_pendulum.rbl > double_pendulum.csv
python3 rbl.py run examples/fractals/mandelbrot_256.rbl > mandelbrot_256.pgm
```

On Windows PowerShell, text CSV output can be redirected with `>` in the usual way. For portable ASCII PGM files, use `cmd /c` for byte-preserving native redirection:

```powershell
cmd /c "python rbl.py run examples\fractals\mandelbrot_256.rbl > mandelbrot_256.pgm"
```

The PGM examples use the plain-text `P2` variant of Netpbm. It is intentionally easy to inspect, parse and open in image tools that support PGM. CSV files include column headers and use decimal values with a dot separator.

## Visualize all examples

The RBL programs perform the calculations; the optional Python pipeline only runs them and converts their PGM/CSV output into viewable images, plots and animations. From the repository root, install the visualization dependencies and run:

```bash
python -m pip install -r requirements-visualize.txt
python tools/visualize_examples.py
```

Generated images, plots and animations go to `build/visualizations/`. The captured PGM/CSV text emitted by RBL is also saved under `build/visualizations/data/` for further analysis:

| RBL program | Visual output |
|---|---|
| `mandelbrot_256.rbl` | `mandelbrot_256.png` |
| `mandelbrot_4k_tile.rbl` | A preview tile `mandelbrot_4k_tile_x0_y0.png`; the complete 3840×2160 image can be assembled from all 30 tiles (6×5). |
| `newton_fractal_512.rbl` | `newton_fractal_512.png`, with a different colour for each attracting root. |
| `white_noise_64.rbl` | `white_noise_64.png` |
| `conway_life.rbl` | `conway_life.gif` and `conway_life_final.png` |
| `double_pendulum.rbl` | `double_pendulum.gif` and `double_pendulum.png` (trajectory and angle plots). |
| `gravity_orbit_3d.rbl` | `gravity_orbit_3d.gif` (XY/XZ projections) and `gravity_orbit_3d.png` (3D trajectories). |

Run one visualization instead of the complete set with a target name:

```bash
python tools/visualize_examples.py life
python tools/visualize_examples.py pendulum
python tools/visualize_examples.py gravity
python tools/visualize_examples.py tile --x 640 --y 432
```

To render the complete 4K Mandelbrot image, run `python tools/visualize_examples.py tile --full`. The full render consists of 30 independent RBL tile runs (6 columns × 5 rows), assembled into a 3840×2160 PNG. To include it in the normal pipeline, pass `--full-4k`; the default `all` run renders only the top-left tile to keep the first run reasonably quick. Animation frame counts can be capped with `--max-frames 200` (the default), and the per-program timeout can be changed with `--timeout 300` (the default is 180 seconds).

The visualization dependencies are optional and are not needed to compile or run RBL programs directly.
The pipeline requires Python 3.10 or newer. Its optional dependencies are listed in `requirements-visualize.txt`.

## Simulations

| Program | What it demonstrates | Output |
|---|---|---|
| [`double_pendulum.rbl`](simulations/double_pendulum.rbl) | A two-link pendulum integrated with fourth-order Runge–Kutta (RK4). The derivative is computed once per RK4 stage and returned as a tuple. | CSV trajectory: step, time, angles, angular velocities and Cartesian bob coordinates. 3,001 samples over 60 simulated seconds. |
| [`gravity_orbit_3d.rbl`](simulations/gravity_orbit_3d.rbl) | Six mutually interacting bodies, pairwise Newtonian gravity, softened distances and velocity-Verlet integration. Pair forces are evaluated once and applied to both bodies. | CSV columns: step, simulation time in seconds, body, mass, position and velocity. One row per body per sampled step: 4,001 time samples and 24,006 data rows. Units are arbitrary and `G = 1`. |
| [`conway_life.rbl`](simulations/conway_life.rbl) | Conway's Game of Life on a finite 256×256 grid. The initial 256-cell population is seeded as 19 randomly grown connected clusters. Neighbour counts are computed over the active frontier using reusable dense arrays. | CSV rows `(generation,population,x,y)`, one row per living cell every two generations. An empty population is represented by a single row with blank coordinates. |

### Gravity model notes

The gravity example uses a small softening term in the squared distance to avoid singular acceleration at near-zero separation. Initial accelerations are evaluated before the first half-kick; this is required for the first Velocity-Verlet step to follow the same scheme as later steps. The star is allowed to move under the planets' gravity rather than being artificially fixed at the origin.

The simulation uses arbitrary units, not a calibrated model of the Solar System. Inclinations and phases are in radians. The chosen masses and initial orbital velocities are illustrative.

## Fractals

| Program | What it demonstrates | Output |
|---|---|---|
| [`mandelbrot_256.rbl`](fractals/mandelbrot_256.rbl) | Escape-time rendering of the Mandelbrot set with 128 maximum iterations. | PGM P2 image, 256×256 pixels. |
| [`mandelbrot_4k_tile.rbl`](fractals/mandelbrot_4k_tile.rbl) | Rendering a tile in a shared 3840×2160 coordinate space. It is suitable for splitting a large image into independent jobs. | PGM P2 tile, 640×432 pixels. Reads `x_offset` and `y_offset` as two separate integer lines from standard input. |
| [`newton_fractal_512.rbl`](fractals/newton_fractal_512.rbl) | Newton iteration for the complex polynomial `z³ − 1`. Each pixel is iterated once; the program returns the nearest root and iteration count together. | PGM P2 image, 512×512 pixels, with intensity encoding the attracting root and convergence speed. |

The 4K tile spans a 3840×2160 image. For a regular 6×5 tiling, use x offsets `0, 640, 1280, 1920, 2560, 3200` and y offsets `0, 432, 864, 1296, 1728`. Each offset pair is two lines in a text input file.

Linux example:

```bash
printf '0\n0\n' | python3 rbl.py run examples/fractals/mandelbrot_4k_tile.rbl > tile_0_0.pgm
```

Windows example, with `tile_offsets.txt` containing `0` and `0` on separate lines:

```powershell
cmd /c "python rbl.py run examples\fractals\mandelbrot_4k_tile.rbl < tile_offsets.txt > tile_0_0.pgm"
```

## Generators

| Program | What it demonstrates | Output |
|---|---|---|
| [`white_noise_64.rbl`](generators/white_noise_64.rbl) | A grayscale white-noise image generated directly from RBL's pseudorandom integer function. | PGM P2 image, 64×64 pixels. |
| [`random_mosaic.rbl`](generators/random_mosaic.rbl) | Random axis-aligned rectangles drawn from a fixed character palette. | A 48×18 ASCII mosaic printed directly in the terminal; it does not read or write PNG files. |

The built-in pseudorandom generator is not cryptographically secure. Its sequences are not guaranteed to match between Linux and Windows, and the examples do not promise seeded, reproducible output.

## Algorithms

These examples calculate and print their results directly from RBL. They do not need the optional Python visualization pipeline.

| Program | What it demonstrates | Output |
|---|---|---|
| [`sorting_search.rbl`](algorithms/sorting_search.rbl) | Merge sort, heap sort, quicksort and binary search. | Sorted arrays, validation flags and search indices. |
| [`sieve_eratosthenes.rbl`](algorithms/sieve_eratosthenes.rbl) | Sieve of Eratosthenes. | All primes up to 200 and their count. |
| [`prime_generation_factorization.rbl`](algorithms/prime_generation_factorization.rbl) | Trial-division prime generation and prime factorization. | Primes up to 100 and the factorization of 360360. |

The binary-search routine expects its input to be sorted. The quicksort implementation uses the final element as its pivot, so already sorted or duplicate-heavy inputs can trigger its quadratic worst case and deep recursion; the example is intended for demonstration-sized arrays.

## Numerical methods

| Program | What it demonstrates | Output |
|---|---|---|
| [`root_finding.rbl`](numerical/root_finding.rbl) | Newton's method and bisection for `x³ − 2x − 5`. | Root estimates, residuals and iteration counts. |
| [`gaussian_elimination.rbl`](numerical/gaussian_elimination.rbl) | Gauss–Jordan elimination with partial pivoting on a 3×3 system. | Solution vector and equation residuals. |
| [`ode_solvers.rbl`](numerical/ode_solvers.rbl) | Fourth-order Runge–Kutta integration for a harmonic oscillator and logistic growth. | Time-series values and oscillator energy. |

## Statistics

| Program | What it demonstrates | Output |
|---|---|---|
| [`shuffle_lab.rbl`](statistics/shuffle_lab.rbl) | Overhand, riffle, cut and Fisher–Yates card shuffles; permutation metrics over 30 repeated trials per point. | Mean and approximate 95% confidence intervals for seven metrics, normalized position entropy and sampled cross-run similarity for 0, 1, 2, 3, 5, 7, 10, 15 and 20 passes. |

The SHUFFLE_LAB port uses RBL's built-in pseudorandom generator. Its sequences are not expected to match the original Python implementation or to be identical across operating systems. It validates every generated deck as a 52-card permutation and runs identity/reverse-permutation metric self-tests. The port does not yet reproduce the original command-line options, arbitrary sweep configuration, packet-count metric, per-experiment CSV export or exact Python PRNG stream. Its reported 95% confidence intervals use a normal approximation (`1.96 × standard error`) with 30 experiments; this is an approximate descriptive interval, not a small-sample Student-t interval. For bounded metrics, the approximation can extend beyond the metric's valid range.

## Physics

| Program | What it demonstrates | Output |
|---|---|---|
| [`magnetism_sandbox.rbl`](simulations/magnetism_sandbox.rbl) | Superposition of 2D point-dipole fields from two permanent magnets and an equivalent coil, with a smooth local material multiplier. | ASCII field-direction map, probe field, dipole energy, finite-difference force and torque. |

The magnetism example is an educational approximation, not a full Maxwell, FEM or MHD solver. Sources are point dipoles with a softened near-field distance. The material is represented by a smooth local field multiplier rather than a solved magnetic constitutive boundary problem. The example is a pure RBL CLI program: it does not use Tkinter, OpenCL, Python calculations or an image backend.

## Check and run the standalone programs

From the repository root, each program can be checked and executed directly:

```bash
python3 rbl.py check examples/algorithms/sorting_search.rbl
python3 rbl.py run examples/algorithms/sorting_search.rbl
python3 rbl.py check examples/algorithms/sieve_eratosthenes.rbl
python3 rbl.py run examples/algorithms/sieve_eratosthenes.rbl
python3 rbl.py check examples/algorithms/prime_generation_factorization.rbl
python3 rbl.py run examples/algorithms/prime_generation_factorization.rbl
python3 rbl.py check examples/generators/random_mosaic.rbl
python3 rbl.py run examples/generators/random_mosaic.rbl
python3 rbl.py check examples/numerical/root_finding.rbl
python3 rbl.py run examples/numerical/root_finding.rbl
python3 rbl.py check examples/numerical/gaussian_elimination.rbl
python3 rbl.py run examples/numerical/gaussian_elimination.rbl
python3 rbl.py check examples/numerical/ode_solvers.rbl
python3 rbl.py run examples/numerical/ode_solvers.rbl
python3 rbl.py check examples/statistics/shuffle_lab.rbl
python3 rbl.py run examples/statistics/shuffle_lab.rbl
python3 rbl.py check examples/simulations/magnetism_sandbox.rbl
python3 rbl.py run examples/simulations/magnetism_sandbox.rbl
```

To check every RBL source in this directory on Linux/macOS:

```bash
find examples -name '*.rbl' -print0 | while IFS= read -r -d '' file; do
    python3 rbl.py check "$file" || exit 1
done
```

On Windows PowerShell:

```powershell
Get-ChildItem examples -Recurse -Filter *.rbl | ForEach-Object {
    python rbl.py check $_.FullName
    if ($LASTEXITCODE -ne 0) { throw "RBL check failed: $($_.FullName)" }
}
```

## Implementation details worth reading

- **Streaming output:** image pixels and simulation records are printed incrementally. The examples do not build one enormous string and then write it at the end. This avoids unnecessary string copies and is a better fit for the current RBL runtime's string-lifetime limitations.
- **Tuples as small result records:** the pendulum derivative, orbital initial state and Newton-fractal sample return several related values in a tuple.
- **Lists passed to functions:** the N-body acceleration routine accepts typed list parameters and updates the arrays in place.
- **Bounded loops and explicit state:** loop temporaries are declared before their loops and updated with `let`, following the current function-level variable model.
- **Portable text formats:** CSV and PGM keep the RBL computation independent from any optional renderer or plotting tool.

## Scope and validation

These programs target RBL 0.7.2 and its implemented language/runtime behavior. They are illustrative numerical workloads, not production-grade physical models or performance benchmarks. The 24 pre-existing RBL files passed `check` and `run` during the documentation review; the 4K visualization path was also exercised by rendering and assembling all 30 tiles. The ten Beginner Course programs were checked and run separately; see the course README for their scope.

The nine standalone programs listed under Algorithms, Generators, Numerical methods, Statistics and Physics also received a focused source/result review and fresh individual `check`/`run` passes. Additional edge-case runs covered sorting empty, singleton, sorted, reverse-sorted and all-equal arrays; a sieve limit of 1; and Gaussian elimination after uniformly scaling the augmented system by `10^-15`. These checks passed. This is targeted example validation, not exhaustive proof for arbitrary inputs.

Known scope limits include the quicksort worst case noted above, the approximate SHUFFLE_LAB intervals, and the educational point-dipole approximation in the magnetism example. The numerical examples use fixed demonstration inputs; they do not expose general-purpose input validation or claim production-grade numerical stability. Their source files can be checked individually:

```bash
python3 rbl.py check examples/simulations/conway_life.rbl
python3 rbl.py check examples/simulations/double_pendulum.rbl
python3 rbl.py check examples/simulations/gravity_orbit_3d.rbl
python3 rbl.py check examples/fractals/mandelbrot_256.rbl
python3 rbl.py check examples/fractals/mandelbrot_4k_tile.rbl
python3 rbl.py check examples/fractals/newton_fractal_512.rbl
python3 rbl.py check examples/generators/white_noise_64.rbl
```

The repository's [regression and smoke-test guide](../docs/eng/how-to/testing.md) describes the compiler test suites. These example programs are demonstrations, not compiler regression cases.
