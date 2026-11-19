#!/usr/bin/env python3
"""Make the two imaginary devices from two real ones.

PIC16F1032 and PIC16F1064 do not exist. They are the largest device each
bank-select encoding allows, for testing the toolchain without a real part's
memory limits getting in the way:

  PIC16F1032  the PIC16F1937 (32 banks, 5-bit MOVLB) with general RAM in
              every bank that can have it (0 to 30: 2,480 bytes) and 32K
              words of flash. The PIC16F1937's LCD data registers lie where
              that RAM now is and are left out.
  PIC16F1064  the PIC16F18877 (64 banks, 6-bit MOVLB), which already has all
              the general RAM a 64-bank device can have (banks 0 to 50: 4,080
              bytes) and 32K words of flash. Only the name differs.

Everything else (special function registers, configuration words, the
peripherals the simulator models) is the real device's.

Usage: gen_imaginary.py <devices-dir>
"""
import json
import os
import sys


def derive(devices, base, name, note, full_ram_banks=None, flash_words=None):
    dev = json.load(open(os.path.join(devices, base + ".json")))
    dev["source"] = dict(dev["source"])
    dev["source"]["imaginary"] = f"{note} Derived from {dev['name']} by tools/devdb/gen_imaginary.py."
    dev["name"] = name
    if flash_words:
        dev["flash"] = {"pages": [{"begin": b, "end": b + 2048} for b in range(0, flash_words, 2048)],
                        "words": flash_words}
    if full_ram_banks:
        dev["gpr"] = [{"begin": bank * 128 + 0x20, "end": bank * 128 + 0x70, "bank": bank}
                      for bank in range(full_ram_banks)]
        dev["gpr_bytes"] = 80 * full_ram_banks
        ram = lambda addr: any(g["begin"] <= addr < g["end"] for g in dev["gpr"])
        gone = {s["name"] for s in dev["sfrs"] if ram(s["addr"])}
        dev["sfrs"] = [s for s in dev["sfrs"] if s["name"] not in gone]
        dev["joined_sfrs"] = [j for j in dev["joined_sfrs"]
                              if not ram(j["addr"]) and not ram(j["addr"] + j["width"] // 8 - 1)]
    with open(os.path.join(devices, name.lower() + ".json"), "w") as f:
        json.dump(dev, f, indent=1)
        f.write("\n")
    return dev


def main():
    devices = sys.argv[1]
    derive(devices, "pic16f1937", "PIC16F1032",
           "An imaginary device: the largest a 32-bank part can be.",
           full_ram_banks=31, flash_words=32768)
    derive(devices, "pic16f18877", "PIC16F1064",
           "An imaginary device: the largest a 64-bank part can be.")


if __name__ == "__main__":
    main()
