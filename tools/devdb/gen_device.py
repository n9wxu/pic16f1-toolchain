#!/usr/bin/env python3
"""Build a device-database entry from a Microchip Device Family Pack .PIC file.

Every fact in the output comes from the .PIC file; the pack name, version and
file hash are recorded so each field can be traced to its source.

Usage: gen_device.py <pack-root> <device> <out.json>
  pack-root: an unpacked DFP, e.g. .../PIC12-16F1xxx_DFP/1.10.270
"""
import hashlib
import json
import os
import sys
import xml.etree.ElementTree as ET

NS = "{http://crownking/edc}"


def a(el, name, default=None):
    return el.get(NS + name, default)


def num(s):
    """Parse a number attribute; None if the pack does not give it."""
    return None if s is None else int(s, 0)


def fields_of(mode):
    """Fields are listed LSB first; AdjustPoint skips unnamed bits."""
    out, bit = [], 0
    for child in mode:
        tag = child.tag[len(NS):]
        if tag == "AdjustPoint":
            bit += num(a(child, "offset"))
        elif tag in ("SFRFieldDef", "DCRFieldDef"):
            width = num(a(child, "nzwidth"))
            f = {"name": a(child, "cname"), "bit": bit, "width": width}
            if tag == "DCRFieldDef":
                vals = {}
                for sem in child.findall(NS + "DCRFieldSemantic"):
                    when = a(sem, "when", "")
                    # form: "(field & 0x7) == 0x4"
                    if "==" in when and a(sem, "cname"):
                        vals[a(sem, "cname")] = num(when.split("==")[1].strip())
                f["values"] = vals
            out.append(f)
            bit += width
    return out


def access_mask(access):
    """Bits whose access character is '-' are unimplemented. MSB first."""
    m = 0
    for i, ch in enumerate(access):
        if ch != "-":
            m |= 1 << (len(access) - 1 - i)
    return m


def reset_bits(s):
    """POR string, MSB first -> (value, known-mask). x/u/q/- count as 0."""
    val = known = 0
    for i, ch in enumerate(s):
        b = len(s) - 1 - i
        if ch in "01":
            known |= 1 << b
            if ch == "1":
                val |= 1 << b
    return val, known


def main():
    pack, device, out = sys.argv[1], sys.argv[2], sys.argv[3]
    pic_path = os.path.join(pack, "edc", device.upper() + ".PIC")
    raw = open(pic_path, "rb").read()
    root = ET.fromstring(raw)
    pdsc = [f for f in os.listdir(pack) if f.endswith(".pdsc")][0]
    pack_name = pdsc[: -len(".pdsc")]
    pack_version = os.path.basename(os.path.normpath(pack))

    dev = {
        "name": a(root, "name"),
        "arch": a(root, "arch"),
        "source": {
            "pack": pack_name,
            "version": pack_version,
            "file": "edc/" + os.path.basename(pic_path),
            "sha256": hashlib.sha256(raw).hexdigest(),
            "license": "Apache-2.0 (Microchip Technology Inc.)",
        },
    }

    mt = root.find(f"{NS}ArchDef/{NS}MemTraits")
    dev["hw_stack_depth"] = num(a(mt, "hwstackdepth"))

    ps = root.find(NS + "ProgramSpace")
    pages = [
        {"begin": num(a(c, "beginaddr")), "end": num(a(c, "endaddr"))}
        for c in ps.findall(NS + "CodeSector")
    ]
    dev["flash"] = {"pages": pages, "words": max(p["end"] for p in pages)}
    uid = ps.find(NS + "UserIDSector")
    if uid is not None:
        dev["user_id"] = {"begin": num(a(uid, "beginaddr")), "end": num(a(uid, "endaddr"))}
    did = ps.find(NS + "DeviceIDSector")
    if did is not None:
        dev["device_id"] = {
            "addr": num(a(did, "beginaddr")),
            "mask": num(a(did, "mask")),
            "value": num(a(did, "value")),
        }
    ee = ps.find(NS + "EEDataSector")
    if ee is not None:
        dev["eeprom"] = {
            "hex_begin": num(a(ee, "beginaddr")),
            "bytes": num(a(ee, "endaddr")) - num(a(ee, "beginaddr")),
        }
    cfg = []
    for sec in ps.findall(NS + "ConfigFuseSector"):
        for d in sec.findall(NS + "DCRDef"):
            mode = d.find(f"{NS}DCRModeList/{NS}DCRMode")
            cfg.append({
                "name": a(d, "cname"),
                "addr": num(a(d, "_addr")),
                "default": num(a(d, "default")),
                "impl": num(a(d, "impl")),
                "fields": fields_of(mode) if mode is not None else [],
            })
    dev["config"] = cfg

    ds = root.find(NS + "DataSpace")
    dev["data_end"] = num(a(ds, "endaddr"))
    # 128 bytes per bank: 32 banks (5-bit BSR) or 64 banks (6-bit BSR).
    dev["bank_count"] = dev["data_end"] // 128
    lin = next(ds.iter(NS + "LinearDataSector"), None)
    if lin is not None:
        dev["linear"] = {"begin": num(a(lin, "beginaddr")), "end": num(a(lin, "endaddr"))}
    gpr, common, sfrs, joined = [], None, [], []
    for sec in ds.iter(NS + "GPRDataSector"):
        if a(sec, "shadowidref"):
            continue
        r = {"begin": num(a(sec, "beginaddr")), "end": num(a(sec, "endaddr"))}
        if a(sec, "regionid") == "gprnobank":
            common = r
        else:
            r["bank"] = num(a(sec, "bank"))
            gpr.append(r)
    gpr.sort(key=lambda r: r["begin"])
    dev["gpr"] = gpr
    dev["common_ram"] = common
    dev["gpr_bytes"] = sum(r["end"] - r["begin"] for r in gpr)

    core_mirror = 0
    for sec in ds.iter(NS + "SFRDataSector"):
        for m in sec.findall(NS + "Mirror"):
            core_mirror = max(core_mirror, num(a(m, "nzsize")))
        for s in sec.iter(NS + "SFRDef"):  # includes halves of joined SFRs
            access = a(s, "access")
            por, por_known = reset_bits(a(s, "por"))
            mode = s.find(f"{NS}SFRModeList/{NS}SFRMode")
            sfrs.append({
                "name": a(s, "cname"),
                "addr": num(a(s, "_addr")),
                "impl": access_mask(access),
                "access": access,
                "por": por,
                "por_known": por_known,
                "fields": fields_of(mode) if mode is not None else [],
            })
        for j in sec.findall(NS + "JoinedSFRDef"):
            joined.append({
                "name": a(j, "cname"),
                "addr": num(a(j, "_addr")),
                "width": num(a(j, "nzwidth")),
            })
    sfrs.sort(key=lambda s: s["addr"])
    joined.sort(key=lambda s: s["addr"])
    dev["core_register_count"] = core_mirror
    dev["sfrs"] = sfrs
    dev["joined_sfrs"] = joined

    with open(out, "w") as f:
        json.dump(dev, f, indent=1)
        f.write("\n")
    print(f"{dev['name']}: {dev['flash']['words']} words flash, "
          f"{dev['gpr_bytes']} bytes GPR, {len(sfrs)} SFRs -> {out}")


if __name__ == "__main__":
    main()
