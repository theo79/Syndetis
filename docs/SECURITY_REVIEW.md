# Publication review

Only the new Syndetis5C project is staged. No parent dirty files, Git administration, raw field logs/photos/captures, tokens, private keys, customer identities, installed toolchains, old source trees or external preservation archive are included.

The unchanged firmware has a deliberately public prototype AP passphrase in main/access_config.h. It is a prototype default already documented in project source, not a personal/customer secret. It is retained in source and saved firmware to preserve accepted behavior; change it in a separately reviewed deployment build before distribution. Packaging does not silently change credentials or firmware behavior. Management IP 192.168.4.1 and default Manual draft 192.168.10.250 are product defaults. 192.0.2.0/24 and 203.0.113.0/24 test addresses and locally administered 02:... fixture MACs are synthetic.

Private DHCP field addresses and original absolute machine paths were removed from this portable validation copy. Original local evidence is untouched. Recovery bins are selected known app/bootloader/table files, not NVS/flash dumps; they contain no captured customer settings. Review scripts/checksums and keep the destination repository PRIVATE. Git credentials are never placed in project files or logs.
