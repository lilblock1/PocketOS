"""Synthetic R1 OTA format tests. No image is suitable for installation."""
import hashlib
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch
import r1_ota_format as o


def chunks(directory, name, payload):
    digest = hashlib.md5(payload).hexdigest()
    previous = digest
    lines = []
    for idx in range(0, len(payload), o.CHUNK):
        data = payload[idx:idx+o.CHUNK]
        number = idx // o.CHUNK
        (directory / f"{name}.{number:04d}.{previous}").write_bytes(data)
        previous = hashlib.md5(data).hexdigest()
        lines.append(previous)
    (directory / f"ota_md5_{name}.{digest}").write_text("\n".join(lines) + "\n")
    return len(payload), digest


def synthetic(directory):
    iso = directory / "source"
    ota = iso / "ota_v0"
    ota.mkdir(parents=True)
    (iso / "ota_config.in").write_text("current_version=0\n")
    (ota / "ota_compatibility.in").write_text("compatibility=hiby_r1_x1600_auto_rf_auto\n")
    (ota / "ota_v0.ok").write_text("ota_v0\n")
    first = chunks(ota, "xImage", b"kernel-test" * 140)
    second = chunks(ota, "rootfs.squashfs", b"original-rootfs" * 210)
    (ota / "ota_update.in").write_text(
        "ota_version=0\n"
        f"img_type=kernel\nimg_name=xImage\nimg_size={first[0]}\nimg_md5={first[1]}\n"
        f"img_type=rootfs\nimg_name=rootfs.squashfs\nimg_size={second[0]}\nimg_md5={second[1]}\n"
    )
    return iso


class Format(unittest.TestCase):
    def test_original_and_replaced_rootfs(self):
        with tempfile.TemporaryDirectory() as temp:
            base = Path(temp)
            iso = synthetic(base)
            original = o.validate_tree(iso)
            payload = b"new-squashfs-payload" * 300
            img = base / "candidate.squashfs"
            img.write_bytes(payload)
            result = o.replace_rootfs(iso, img)
            self.assertEqual(result["rootfs"]["md5"], hashlib.md5(payload).hexdigest())
            self.assertEqual(o.validate_tree(iso)["kernel"], original["kernel"])
            self.assertEqual(o.metadata(iso/"ota_v0/ota_update.in")["rootfs"]["img_size"], str(len(payload)))

    def test_corrupt_chunk_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            iso = synthetic(Path(temp))
            r = o.metadata(iso/"ota_v0/ota_update.in")["rootfs"]
            chunk = iso/"ota_v0"/f"rootfs.squashfs.0000.{r['img_md5']}"
            chunk.write_bytes(chunk.read_bytes()[:-1]+b"X")
            with self.assertRaises(ValueError):
                o.validate_tree(iso)

    def test_bad_manifest_and_symlink_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            base = Path(temp)
            iso = synthetic(base)
            (iso/"ota_v0"/"danger").symlink_to(base)
            with self.assertRaises(ValueError):
                o.validate_tree(iso)
            (iso/"ota_v0"/"danger").unlink()
            (iso/"ota_v0"/"ota_update.in").write_text("img_type=rootfs\n")
            with self.assertRaises(ValueError):
                o.validate_tree(iso)

    def test_roundtrip_helper_rejects_existing_outputs(self):
        with tempfile.TemporaryDirectory() as temp:
            base = Path(temp)
            iso = synthetic(base)
            img = base/"new.img"
            img.write_bytes(b"new")
            out = base/"test.upt"
            out.write_bytes(b"existing")
            with self.assertRaises(ValueError):
                o.build_and_verify(iso,img,out,base/"report.json")


if __name__ == "__main__":
    unittest.main()
