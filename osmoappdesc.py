#!/usr/bin/env python3

app_configs = {
    "osmo-smlc": ["doc/examples/osmo-smlc/osmo-smlc.cfg"]
}

apps = [(4271, "src/osmo-smlc/osmo-smlc", "OsmoSMLC", "osmo-smlc")
        ]

vty_command = ["./src/osmo-smlc/osmo-smlc", "-c",
               "doc/examples/osmo-smlc/osmo-smlc.cfg"]

vty_app = apps[0]
