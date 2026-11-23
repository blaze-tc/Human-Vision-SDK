import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parents[1] / "patch_runtime_prefab_live_skeleton.py"
spec = importlib.util.spec_from_file_location("live_patch", SCRIPT)
patch = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patch)
PREFIXES = json.loads((Path(__file__).parent / "fixtures/runtime_prefab_live_skeleton_prefixes.json").read_text(encoding="utf-8"))
SUFFIX = "        internal void Draw(Rect image, float width) { /* private drawing extension */ }\n"
def skeleton(prefix): return "// private header\n" + prefix + SUFFIX

def private_preview():
    return patch.PRIVATE_PREVIEW_REPLACEMENTS[0][0] + patch.OLD_FULLSCREEN + "\n" + patch.PRIVATE_PREVIEW_REPLACEMENTS[2][0] + "\n" + patch.SIDEBAR_PREVIEW_REPLACEMENTS[1][0] + "\n" + patch.SIDEBAR_PREVIEW_REPLACEMENTS[2][0] + "\n// exact-photo private extension\n"
def private_main():
    text = patch.SIDEBAR_OLD_ON_GUI[1]
    for before, after in patch.PRIVATE_MAIN_REPLACEMENTS:
        if before not in text and after not in text: text += "\n" + after
    for before, after in patch.SIDEBAR_MAIN_COMMON:
        if before not in text: text += "\n" + before
    return text + "\n// untouched settings controls\n"

class LivePatchSafetyTests(unittest.TestCase):
    def test_both_reviewed_prefixes_upgrade_without_replacing_private_drawing(self):
        for prefix in PREFIXES.values():
            self.assertEqual(skeleton(patch.QUERY_PREFIX), patch.patch_live_skeleton(skeleton(prefix)))
    def test_replay_is_idempotent(self):
        after = patch.patch_live_skeleton(skeleton(PREFIXES["configured"]))
        self.assertEqual(after, patch.patch_live_skeleton(after))
    def test_unknown_owned_prefix_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "Unknown"):
            patch.patch_live_skeleton(skeleton(PREFIXES["configured"].replace("Capacity = 8", "Capacity = 7")))
    def test_ambiguous_anchor_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "anchors"):
            patch.patch_live_skeleton(skeleton(PREFIXES["configured"]) + SUFFIX)
    def test_crlf_source_keeps_crlf(self):
        after = patch.patch_live_skeleton(skeleton(PREFIXES["configured"]).replace("\n", "\r\n"))
        self.assertEqual(after.count("\n"), after.count("\r\n"))
    def test_preview_upgrade_keeps_photo_extension_and_replays(self):
        after = patch.patch_preview(private_preview())
        self.assertIn("// exact-photo private extension", after)
        self.assertEqual(after, patch.patch_preview(after))
        self.assertNotIn("liveSkeleton.Configure", after)
    def test_single_pane_upgrade_keeps_settings_controls_and_replays(self):
        after = patch.patch_acceptance(private_main())
        self.assertIn("// untouched settings controls", after)
        self.assertEqual(after, patch.patch_acceptance(after))
        self.assertEqual(1, after.count("DrawUnifiedPreviewCard(sidebar, preview)"))

class LivePatchStagingTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(); self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name); self.project = self.root / "Human-Vision-SDK-Test"; self.backup = self.root / "backup"
        self.folder = self.project / "Assets/HumanVision/Samples/RuntimePrefab"; self.folder.mkdir(parents=True)
        texts = {"RuntimePrefabLiveSkeleton.cs": skeleton(PREFIXES["configured"]), "RuntimePrefabAcceptancePreview.cs": private_preview(), "RuntimePrefabAcceptance.cs": private_main()}
        for name, value in texts.items(): (self.folder / name).write_bytes(b"\xef\xbb\xbf" + value.replace("\n", "\r\n").encode())
        self.before = {p.name: p.read_bytes() for p in self.folder.iterdir()}
    def test_dry_run_never_writes_or_creates_backups(self):
        receipt = patch.stage(self.project, self.backup)
        self.assertEqual(4, sum(item["changed"] for item in receipt["files"]))
        self.assertFalse(self.backup.exists())
        self.assertEqual(self.before, {p.name: p.read_bytes() for p in self.folder.iterdir()})
    def test_apply_backs_up_original_bytes_and_preserves_bom_and_replays(self):
        patch.stage(self.project, self.backup, True)
        for name, value in self.before.items():
            self.assertEqual(value, (self.backup / "Assets/HumanVision/Samples/RuntimePrefab" / name).read_bytes())
            self.assertTrue((self.folder / name).read_bytes().startswith(b"\xef\xbb\xbf"))
        self.assertFalse(any(item["changed"] for item in patch.stage(self.project, self.backup, True)["files"]))
    def test_unknown_later_file_prevents_all_writes(self):
        (self.folder / "RuntimePrefabAcceptance.cs").write_text("unknown();", encoding="utf-8")
        with self.assertRaises(ValueError): patch.stage(self.project, self.backup, True)
        self.assertEqual(self.before["RuntimePrefabLiveSkeleton.cs"], (self.folder / "RuntimePrefabLiveSkeleton.cs").read_bytes())
        self.assertFalse(self.backup.exists())
    def test_conflicting_backup_prevents_all_writes(self):
        saved=self.backup / "Assets/HumanVision/Samples/RuntimePrefab/RuntimePrefabAcceptance.cs";saved.parent.mkdir(parents=True);saved.write_text("unrelated",encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "backup"): patch.stage(self.project, self.backup, True)
        self.assertEqual(self.before, {p.name: p.read_bytes() for p in self.folder.iterdir()})
    def test_unknown_sidebar_payload_prevents_all_source_writes(self):
        target=self.folder / "RuntimePrefabAcceptanceSidebar.cs";target.write_text("private user component",encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "sidebar"): patch.stage(self.project, self.backup, True)
        for name,value in self.before.items():self.assertEqual(value,(self.folder/name).read_bytes())
        self.assertEqual("private user component",target.read_text(encoding="utf-8"))
        self.assertFalse(self.backup.exists())
    def test_other_project_name_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "Human-Vision-SDK-Test"): patch.stage(self.root / "other", self.backup)

if __name__ == "__main__": unittest.main()
