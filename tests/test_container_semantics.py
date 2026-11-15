#!/usr/bin/env python3
"""Runtime regressions for dictionary key/hash contracts."""
from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import rbltool  # noqa: E402


class ContainerSemanticsTests(unittest.TestCase):
    def test_container_values_are_rejected_as_dictionary_keys(self) -> None:
        cases = {
            "list": "set key = [1]\n",
            "dictionary": "set key = {}\n",
            "tuple": "set key = (1, 2)\n",
        }
        with tempfile.TemporaryDirectory(prefix="rbl-container-key-") as temp:
            for kind, declaration in cases.items():
                with self.subTest(kind=kind):
                    source = Path(temp) / f"{kind}.rbl"
                    source.write_text(
                        "func main() (\n"
                        f"    {declaration}"
                        "    set values = {}\n"
                        "    let values[key] = 1\n"
                        ")\n",
                        encoding="utf-8",
                    )
                    code, stdout, stderr = rbltool.run_program(source, {})
                    self.assertNotEqual(code, 0, stdout)
                    self.assertIn("dictionary keys must be scalar", stderr)


if __name__ == "__main__":
    unittest.main(verbosity=2)
