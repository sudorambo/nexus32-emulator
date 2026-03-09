# Test fixtures

## minimal.nxrom

Minimal valid NEXUS-32 ROM used for CI and local testing. It was generated with **nexus32-romtools**: from the romtools repo, build rompack, then in `test/minimal` run:

```bash
rompack -o minimal.nxrom -c pack.toml
```

Copy the resulting `minimal.nxrom` here. If the ROM format changes (spec or romtools), regenerate this file from romtools and commit the update.
