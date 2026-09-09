import importlib.util
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("validate_skills", Path(__file__).resolve().parents[2] / "Build/validate_skills.py")
validator = importlib.util.module_from_spec(spec)
spec.loader.exec_module(validator)


class SkillValidationTests(unittest.TestCase):
    def validate(self, text):
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory) / "example-skill"
            folder.mkdir()
            if text is not None:
                (folder / "SKILL.md").write_text(text, encoding="utf-8")
            return validator.validate_skill(folder)

    def test_unicode_and_multiline_description(self):
        self.assertIsNone(self.validate("---\nname: example-skill\ndescription: |\n  Validate français\n  and 日本語.\n---\nRead the contract.\n"))

    def test_invalid_front_matter(self):
        for text in (None, "instructions only", "---\n[\n---\nbody", "---\n- list\n---\nbody"):
            with self.subTest(text=text):
                self.assertIsNotNone(self.validate(text))

    def test_metadata_constraints(self):
        for metadata in ("name: wrong-name\ndescription: valid", "name: example-skill\ndescription: 123", "name: example-skill\ndescription: ''", "name: example-skill\ndescription: <unsafe>", "name: example-skill\ndescription: valid\nunexpected: value"):
            with self.subTest(metadata=metadata):
                self.assertIsNotNone(self.validate(f"---\n{metadata}\n---\nbody"))

    def test_unfinished_instructions_and_description(self):
        prefix = "---\nname: example-skill\ndescription: valid\n---\n"
        self.assertIsNotNone(self.validate(prefix))
        self.assertIsNotNone(self.validate(prefix + "[TODO: finish]\n"))
        self.assertIsNone(self.validate(prefix + "```text\n[TODO: example]\n```\n"))
        self.assertIsNotNone(self.validate("---\nname: example-skill\ndescription: '[TODO: finish]'\n---\nbody"))


if __name__ == "__main__":
    unittest.main()
