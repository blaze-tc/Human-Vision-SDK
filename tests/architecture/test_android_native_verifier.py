"""Focused symbol-closure regressions for the Android ELF verifier."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "verify_android_native", ROOT / "tools" / "test" / "verify_android_native.py"
)
VERIFIER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(VERIFIER)


class AndroidNativeVerifierTests(unittest.TestCase):
    def make_file(self, root, name):
        path = root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(b"fixture")
        return path

    def test_transitive_missing_import_names_importing_library(self):
        with tempfile.TemporaryDirectory() as temp:
            temp = Path(temp)
            packaged = temp / "packaged"
            system = temp / "system"
            root = self.make_file(temp, "libhumanvision.so")
            dep = self.make_file(packaged, "libdependency.so")

            symbols = {
                root: ({"hv_entry"}, {"dep_entry"}),
                dep: ({"dep_entry"}, {"api_27_only"}),
            }
            needed = {root: ["libdependency.so"], dep: []}

            with self.assertRaisesRegex(AssertionError, r"libdependency\.so.*api_27_only"):
                VERIFIER.validate_dynamic_closure(
                    root, needed[root], [packaged], [system],
                    lambda path: symbols[path], lambda path: needed[path]
                )

    def test_matching_default_export_satisfies_versioned_and_unversioned_imports(self):
        parsed_exports, _ = VERIFIER.parse_dynamic_symbols(
            "204: 00000000 8 FUNC GLOBAL DEFAULT 9 malloc@@LIBC\n"
            "205: 00000000 8 FUNC GLOBAL DEFAULT 9 calloc@@LIBC\n"
        )
        _, parsed_imports = VERIFIER.parse_dynamic_symbols(
            "61: 00000000 0 FUNC GLOBAL DEFAULT UND malloc@LIBC\n"
            "62: 00000000 0 FUNC GLOBAL DEFAULT UND calloc\n"
        )
        self.assertEqual({"malloc", "malloc@LIBC", "calloc", "calloc@LIBC"}, parsed_exports)
        self.assertEqual({"malloc@LIBC", "calloc"}, parsed_imports)

        with tempfile.TemporaryDirectory() as temp:
            temp = Path(temp)
            system = temp / "system"
            root = self.make_file(temp, "libhumanvision.so")
            libc = self.make_file(system, "libc.so")
            symbols = {
                root: (set(), parsed_imports),
                libc: (parsed_exports, set()),
            }
            needed = {root: ["libc.so"], libc: []}
            VERIFIER.validate_dynamic_closure(
                root, needed[root], [], [system],
                lambda path: symbols[path], lambda path: needed[path]
            )

    def test_absent_version_of_existing_symbol_is_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            temp = Path(temp)
            system = temp / "system"
            root = self.make_file(temp, "libhumanvision.so")
            libc = self.make_file(system, "libc.so")
            _, missing_version = VERIFIER.parse_dynamic_symbols(
                "61: 00000000 0 FUNC GLOBAL DEFAULT UND malloc@LIBC_REVIEW_NONEXISTENT\n"
            )
            symbols = {
                root: (set(), missing_version),
                libc: ({"malloc", "malloc@LIBC"}, set()),
            }
            needed = {root: ["libc.so"], libc: []}
            with self.assertRaisesRegex(
                AssertionError,
                r"libhumanvision\.so.*malloc@LIBC_REVIEW_NONEXISTENT",
            ):
                VERIFIER.validate_dynamic_closure(
                    root, needed[root], [], [system],
                    lambda path: symbols[path], lambda path: needed[path]
                )


if __name__ == "__main__":
    unittest.main()
