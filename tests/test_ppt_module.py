import tempfile
import unittest
import xml.etree.ElementTree as ET
import zipfile
from pathlib import Path

from ppt.generator import generate_project_deck


class PptModuleTest(unittest.TestCase):
    def test_generates_valid_pptx_package(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            output_path = Path(temp_dir) / "deck" / "pifl_overview.pptx"
            generated = generate_project_deck(output_path)

            self.assertEqual(generated, output_path)
            self.assertTrue(output_path.exists())
            self.assertTrue(zipfile.is_zipfile(output_path))

            with zipfile.ZipFile(output_path) as archive:
                names = set(archive.namelist())
                self.assertIn("[Content_Types].xml", names)
                self.assertIn("ppt/presentation.xml", names)
                self.assertIn("ppt/slideMasters/slideMaster1.xml", names)
                self.assertIn("ppt/slideLayouts/slideLayout1.xml", names)
                self.assertIn("ppt/theme/theme1.xml", names)
                self.assertIn("ppt/slides/slide1.xml", names)
                self.assertIn("ppt/slides/slide7.xml", names)

                presentation_root = ET.fromstring(archive.read("ppt/presentation.xml"))
                ns = {
                    "p": "http://schemas.openxmlformats.org/presentationml/2006/main",
                    "a": "http://schemas.openxmlformats.org/drawingml/2006/main",
                }
                slides = presentation_root.findall(".//p:sldId", ns)
                self.assertEqual(len(slides), 7)

                slide1_root = ET.fromstring(archive.read("ppt/slides/slide1.xml"))
                slide7_root = ET.fromstring(archive.read("ppt/slides/slide7.xml"))
                slide1_text = " ".join(node.text or "" for node in slide1_root.findall(".//a:t", ns))
                slide7_text = " ".join(node.text or "" for node in slide7_root.findall(".//a:t", ns))

                self.assertIn("Physics-Informed Federated Learning on ESP8266", slide1_text)
                self.assertIn("pio run", slide7_text)
                self.assertIn("python3 -m unittest tests/test_simulation_run.py", slide7_text)


if __name__ == "__main__":
    unittest.main()
