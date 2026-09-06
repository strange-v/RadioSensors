import importlib.util
import tempfile
import unittest
from pathlib import Path


SCRIPT = Path(__file__).parents[1] / "scripts" / "provision_node.py"
SPEC = importlib.util.spec_from_file_location("provision_node", SCRIPT)
provision_node = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(provision_node)


class ProvisionNodeTests(unittest.TestCase):
    def test_record_round_trip_and_corruption(self):
        key = bytes(range(1, 17))
        record = provision_node.encode_factory_record(key)
        self.assertEqual(32, len(record))
        self.assertEqual(
            "52534643010100000102030405060708090A0B0C0D0E0F10AEDC512300000000",
            record.hex().upper(),
        )
        self.assertEqual(key, provision_node.decode_factory_record(record))
        corrupt = bytearray(record)
        corrupt[8] ^= 1
        self.assertIsNone(provision_node.decode_factory_record(bytes(corrupt)))

    def test_exports_versioned_uppercase_credentials(self):
        uid = bytes.fromhex("102132435465768798A9")
        key = bytes.fromhex("00112233445566778899AABBCCDDEEFF")
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            uri = provision_node.export_credentials(uid, key, output, no_qr=False)
            self.assertEqual(
                "radiosensors://pair?v=1&uid=102132435465768798A9&"
                "key=00112233445566778899AABBCCDDEEFF",
                uri,
            )
            self.assertIn(uri, (output / "102132435465768798A9.txt").read_text())
            self.assertIn("factory_key", (output / "manifest.csv").read_text())
            self.assertTrue((output / "102132435465768798A9.svg").read_text().startswith("<?xml"))


if __name__ == "__main__":
    unittest.main()
