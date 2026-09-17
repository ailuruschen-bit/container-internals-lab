# Labs — Chapter 10: The OCI Specifications

| Lab | Topic | Needs | Doc section |
|---|---|---|---|
| [lab-01-runc-lifecycle](lab-01-runc-lifecycle/) | build a bundle; create/start/state/kill/delete; edit config to change isolation | runc, root | [§2](../../docs/10-oci-runtime-spec/02-runtime-spec-config.md), [§3](../../docs/10-oci-runtime-spec/03-runtime-lifecycle.md) |
| [lab-02-inspect-an-image](lab-02-inspect-an-image/) | index → manifest → config + layers; shared layers; config → config.json | skopeo/crane | [§4](../../docs/10-oci-runtime-spec/04-image-spec.md) |

Packages on Debian/Ubuntu:

```bash
sudo apt-get install -y runc skopeo jq busybox-static util-linux
# crane (optional): go install github.com/google/go-containerregistry/cmd/crane@latest
```

Lab 01 reuses the rootfs from Chapter 07 Lab 01. Lab 02 reads public registry
images read-only. Versions: runc targets runtime-spec v1.2.x; image inspection
follows image-spec v1.1.x. Field names may differ in other spec versions.
